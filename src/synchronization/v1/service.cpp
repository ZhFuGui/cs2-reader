#include "synchronization/v1/service.hpp"
#include "synchronization/v1/subscribers.hpp"
#include "transport/ipv4.hpp"
#include "transport/tcp_server.hpp"
#include <Windows.h>
#include <algorithm>
#include <bcrypt.h>
#include <cmath>
#include <thread>

namespace cs2::synchronization::v1 {
namespace {
InstanceId random_id() {
    InstanceId id{};
    if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(id.data()), static_cast<ULONG>(id.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("Cannot allocate Reader instance identity");
    return id;
}
struct WakeEvent {
    HANDLE value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    WakeEvent() {
        if (!value)
            throw std::runtime_error("Cannot create network wake event");
    }
    ~WakeEvent() { CloseHandle(value); }
    void notify() const noexcept { SetEvent(value); }
};
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
    WakeEvent wake;
    std::atomic_bool stopping{false};
    std::thread encoder_thread, network_thread;

    Impl(const ServiceOptions& config, const capture::IClock& timer, capture::FrequencyControl* control)
        : options(validate(config)), clock(timer), owned_frequency(options.capture_hz),
          identity{random_id(), random_id(), clock.frequency(), options.capture_hz, options.bones_enabled},
          subscribers(identity, publication, control ? *control : owned_frequency, options.capture_hz) {
        try {
            encoder_thread = std::thread([this] { encode_loop(); });
            network_thread = std::thread([this] { network_loop(); });
        } catch (...) {
            shutdown();
            throw;
        }
    }
    ~Impl() { shutdown(); }
    void shutdown() {
        stopping.store(true);
        publication.stop();
        wake.notify();
        if (encoder_thread.joinable())
            encoder_thread.join();
        if (network_thread.joinable())
            network_thread.join();
    }
    void encode_loop() {
        while (auto task = publication.take()) {
            try {
                if (task->snapshot->qpc_frequency != identity.qpc_frequency)
                    throw wire::Error("capture clock changed");
                auto packet = std::make_shared<EncodedSnapshot>();
                packet->bytes = encode_snapshot(*task->snapshot, identity.instance, identity.clock_domain);
                validate_snapshot_size(identity, packet->bytes.size());
                packet->token = task->token;
                packet->sequence = task->snapshot->sequence;
                packet->capture_hz = task->snapshot->capture_target_hz;
                packet->finished_qpc = task->snapshot->interval.finished_qpc;
                if (publication.commit(std::move(packet)))
                    wake.notify();
            } catch (const std::exception& error) {
                if (publication.encoding_failed(task->token, std::string("Encoding: ") + error.what()))
                    wake.notify();
            }
        }
    }
    void network_loop() {
        using Steady = std::chrono::steady_clock;
        constexpr unsigned backoff[] = {100, 200, 500, 1000, 2000, 5000};
        std::size_t attempt = 0;
        while (!stopping.load()) {
            const auto started = Steady::now();
            try {
                transport::TcpServer server(options.port, options.maximum_connections, options.bind_address);
                publication.clear_error("TCP:");
                server.run(subscribers, clock, stopping, wake.value);
                if (stopping.load())
                    return;
            } catch (const std::exception& error) {
                publication.failure(std::string("TCP: ") + error.what());
            }
            if (Steady::now() - started >= std::chrono::seconds(5))
                attempt = 0;
            const auto delay = backoff[std::min(attempt, std::size(backoff) - 1)];
            if (attempt < std::size(backoff) - 1)
                ++attempt;
            const auto until = Steady::now() + std::chrono::milliseconds(delay);
            while (!stopping.load() && Steady::now() < until)
                WaitForSingleObject(wake.value, 50);
        }
    }
};
SubscriptionService::SubscriptionService(const ServiceOptions& options, const capture::IClock& clock,
                                         capture::FrequencyControl* frequency)
    : impl_(std::make_unique<Impl>(options, clock, frequency)) {}
SubscriptionService::~SubscriptionService() = default;
void SubscriptionService::submit(std::shared_ptr<const model::Snapshot> value) {
    if (impl_->publication.submit(std::move(value)))
        impl_->wake.notify();
}
void SubscriptionService::unavailable(model::Availability value, std::string reason,
                                      std::optional<std::uint64_t> epoch) {
    if (impl_->publication.invalidate(value, std::move(reason), epoch))
        impl_->wake.notify();
}
ServiceStats SubscriptionService::stats() const {
    const auto source = impl_->publication.stats();
    const auto clients = impl_->subscribers.stats();
    return {source.encoded, clients.sent, clients.coalesced, source.coalesced, clients.connected, source.error};
}
} // namespace cs2::synchronization::v1
