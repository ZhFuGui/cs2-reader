#include "synchronization/v2/service.hpp"
#include "model/snapshot.hpp"
#include "synchronization/v2/subscribers.hpp"
#include "transport/ipv4.hpp"
#include <Windows.h>
#include <algorithm>
#include <bcrypt.h>
#include <cmath>
#include <thread>

namespace cs2::synchronization::v2 {
namespace {
InstanceId random_id() {
    InstanceId id{};
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(id.data()), static_cast<ULONG>(id.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("Cannot allocate Reader instance identity");
    return id;
}
ServiceOptions validate(ServiceOptions options) {
    if (!std::isfinite(options.capture_hz) || options.capture_hz < 1 || options.capture_hz > 300 || !options.port ||
        !options.maximum_connections || options.maximum_connections > 60 ||
        !transport::parse_ipv4(options.bind_address))
        throw std::runtime_error("Invalid subscription service configuration");
    return options;
}
} // namespace
struct SubscriptionService::Impl final {
    const ServiceOptions options;
    const capture::IClock& clock;
    capture::FrequencyControl owned_frequency;
    const ServerIdentity identity;
    Publication publication;
    Subscribers subscribers;
    std::atomic_bool stopping{false};
    std::thread encoder_thread;
    // 仅编码所有者创建、停止和回收服务；实际网络线程由共享 TcpServer 唯一持有。
    std::unique_ptr<tcp::TcpServer> server;
    using Steady = std::chrono::steady_clock;
    Steady::time_point retry_network{}, healthy_since{};
    std::size_t network_attempt{};

    Impl(const ServiceOptions& config, const capture::IClock& timer, capture::FrequencyControl* control)
        : options(validate(config)), clock(timer), owned_frequency(options.capture_hz),
          identity{random_id(), random_id(), clock.frequency(), options.capture_hz, options.bones_enabled},
          subscribers(identity, publication, control ? *control : owned_frequency, options.capture_hz, clock) {
        try {
            encoder_thread = std::thread([this] { encode_loop(); });
        } catch (...) {
            shutdown();
            throw;
        }
    }
    ~Impl() { shutdown(); }
    void shutdown() {
        stopping.store(true);
        publication.stop();
        if (encoder_thread.joinable())
            encoder_thread.join();
    }
    void stop_network() {
        if (!server)
            return;
        server->request_stop();
        (void)server->join();
        // 回调已经结束，再撤销跨线程唤醒目标；notify 与此交接由 Subscribers 串行保护。
        subscribers.attach(nullptr);
        server.reset();
    }
    void recover_network() {
        const auto now = Steady::now();
        if (server) {
            const auto state = server->status().state;
            if (state != tcp::ServiceState::Failed) {
                if (state == tcp::ServiceState::Listening) {
                    if (healthy_since == Steady::time_point{})
                        healthy_since = now;
                    if (now - healthy_since >= std::chrono::seconds(5))
                        network_attempt = 0;
                } else
                    healthy_since = {};
                return; // 普通监听恢复由共享层负责，不叠加第二套网络重试循环。
            }
            stop_network();
            defer_network();
            return;
        }
        if (now < retry_network)
            return;
        try {
            tcp::TcpOptions transport_options;
            transport_options.port = options.port;
            transport_options.bind_address = options.bind_address;
            transport_options.maximum_connections = options.maximum_connections;
            transport_options.connection_queue.records = 18; // 控制结果和正在发送/最新快照共用有界预算。
            server = std::make_unique<tcp::TcpServer>(std::move(transport_options), subscribers);
            subscribers.attach(server.get());
            if (const auto error = server->start(); error) {
                publication.network_failed(error.native_status ? std::optional(error.native_status) : std::nullopt);
                stop_network();
            } else
                return;
        } catch (const std::exception&) {
            publication.network_failed();
            stop_network();
        }
        defer_network();
    }
    void defer_network() {
        constexpr unsigned backoff[] = {100, 200, 500, 1000, 2000, 5000};
        const auto delay = backoff[std::min(network_attempt, std::size(backoff) - 1)];
        if (network_attempt < std::size(backoff) - 1)
            ++network_attempt;
        healthy_since = {};
        retry_network = Steady::now() + std::chrono::milliseconds(delay + clock.now() % (delay / 10 + 1));
    }
    void encode_loop() {
        while (!stopping.load()) {
            recover_network();
            auto task = publication.take(std::chrono::milliseconds(100));
            if (!task || stopping.load())
                continue;
            try {
                if (task->snapshot->qpc_frequency != identity.qpc_frequency)
                    throw wire::Error("capture clock changed");
                auto packet = std::make_shared<EncodedSnapshot>();
                auto bytes = encode_snapshot(*task->snapshot, identity.instance, identity.clock_domain);
                validate_snapshot_size(identity, bytes.size());
                packet->record = envelope(identity, 0, Message{13, std::move(bytes), {}});
                packet->token = task->token;
                packet->sequence = task->snapshot->sequence;
                packet->capture_hz = task->snapshot->capture_target_hz;
                packet->finished_qpc = task->snapshot->interval.finished_qpc;
                if (publication.commit(std::move(packet)))
                    subscribers.notify();
            } catch (const std::exception&) {
                if (publication.encoding_failed(task->token))
                    subscribers.notify();
            }
        }
        stop_network();
    }
};
SubscriptionService::SubscriptionService(const ServiceOptions& options, const capture::IClock& clock,
                                         capture::FrequencyControl* frequency)
    : impl_(std::make_unique<Impl>(options, clock, frequency)) {}
SubscriptionService::~SubscriptionService() = default;
void SubscriptionService::submit(std::shared_ptr<const model::Snapshot> value) {
    if (impl_->publication.submit(std::move(value)))
        impl_->subscribers.notify();
}
void SubscriptionService::unavailable(model::Availability value, model::ReasonCode reason,
                                      std::optional<std::uint64_t> epoch) {
    if (impl_->publication.invalidate(value, reason, epoch))
        impl_->subscribers.notify();
}
ServiceStats SubscriptionService::stats() const {
    const auto source = impl_->publication.stats();
    const auto clients = impl_->subscribers.stats();
    return {source.encoded,    clients.sent,          clients.coalesced,   source.coalesced,
            clients.connected, source.encoding_fault, source.network_fault};
}
} // namespace cs2::synchronization::v2
