#include "synchronization/v1/service.hpp"
#include "diagnostics/obf.hpp"
#include "cs2_reader/v1/schema.hpp"
#include "transport/tcp_server.hpp"
#include "transport/ipv4.hpp"
#include <Windows.h>
#include <atomic>
#include <bcrypt.h>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace cs2::synchronization::v1 {
namespace {
using Steady = std::chrono::steady_clock;
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
struct Packet {
    wire::Bytes bytes;
    std::uint64_t token{}, sequence{};
    std::uint64_t finished_qpc{};
    double capture_hz{};
};
struct Input {
    std::shared_ptr<const model::Snapshot> snapshot;
    std::uint64_t token{};
};
struct View {
    SourceStatus status;
    std::uint64_t token{}, status_revision{}, invalidation_serial{};
    std::shared_ptr<const Packet> packet;
};
struct Client {
    bool subscribed = false;
    bool follows_capture = false;
    double publish_hz{};
    std::uint64_t event_sequence{}, last_request{}, last_published{}, status_revision{}, invalidations{}, coalesced{};
    std::uint64_t minimum_finished_qpc{};
    std::deque<Message> controls;
    std::shared_ptr<const Packet> pending;
    Steady::time_point next_publish{}, last_sent = Steady::now();
    void enqueue(Message message) {
        if (controls.size() >= 16)
            throw std::runtime_error("control queue resource limit");
        controls.push_back(std::move(message));
    }
};
} // namespace
struct SubscriptionService::Impl final : transport::IRecordHandler {
    ServiceOptions options;
    const capture::IClock& clock;
    capture::FrequencyControl owned_frequency;
    capture::FrequencyControl& frequency;
    ServerIdentity identity;
    WakeEvent wake;
    mutable std::mutex mutex;
    std::condition_variable ready;
    View state;
    std::optional<Input> input;
    std::atomic_bool stopping{false};
    std::thread encoder_thread, network_thread;
    std::map<transport::ConnectionId, Client> clients; // 仅网络线程拥有。
    std::atomic_uint64_t encoded{}, sent_count{}, pending_coalesced{}, encoder_coalesced{}, connected_count{};
    std::string last_error;

    Impl(const ServiceOptions& config, const capture::IClock& timer, capture::FrequencyControl* control)
        : options(config), clock(timer), owned_frequency(config.capture_hz),
          frequency(control ? *control : owned_frequency) {
        if (!std::isfinite(options.capture_hz) || options.capture_hz < 1 || !options.port ||
            !options.maximum_connections || options.maximum_connections > 60 || !transport::parse_ipv4(options.bind_address))
            throw std::runtime_error("Invalid subscription service configuration");
        identity = {random_id(),        random_id(),    clock.frequency(),
                    options.capture_hz, options.hyperv, options.bones_enabled};
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
        if (!stopping.exchange(true)) {
            {
                std::lock_guard lock(mutex);
                ++state.token;
                state.status.availability = Availability::Stopping;
                input.reset();
                state.packet.reset();
            }
            ready.notify_all();
            wake.notify();
        }
        if (encoder_thread.joinable())
            encoder_thread.join();
        if (network_thread.joinable())
            network_thread.join();
    }
    void failure(std::string error) {
        std::lock_guard lock(mutex);
        last_error = std::move(error);
    }
    View view() const {
        std::lock_guard lock(mutex);
        return state;
    }
    void invalidate(Availability availability, std::string reason, std::optional<std::uint64_t> epoch) {
        {
            std::lock_guard lock(mutex);
            if (state.status.availability == availability && state.status.reason == reason &&
                state.status.epoch == epoch)
                return;
            ++state.token;
            ++state.status_revision;
            ++state.invalidation_serial;
            state.status.availability = availability;
            state.status.reason = std::move(reason);
            state.status.epoch = epoch;
            state.packet.reset();
            input.reset();
        }
        wake.notify();
    }
    void submit(std::shared_ptr<const model::Snapshot> snapshot) {
        if (!snapshot || stopping.load())
            return;
        {
            std::lock_guard lock(mutex);
            if (state.status.last_success && snapshot->sequence <= *state.status.last_success)
                throw std::runtime_error("Capture sequence must increase");
            const bool new_epoch = state.status.epoch && *state.status.epoch != snapshot->epoch;
            if (new_epoch || state.status.availability != Availability::Ready) {
                ++state.token;
                ++state.status_revision;
                if (new_epoch)
                    ++state.invalidation_serial;
                state.packet.reset();
            }
            state.status = {Availability::Ready, snapshot->epoch, snapshot->sequence, "CAPTURE_READY"};
            if (input)
                ++encoder_coalesced;
            input = Input{std::move(snapshot), state.token};
        }
        ready.notify_one();
        wake.notify();
    }
    void encode_loop() {
        for (;;) {
            Input task;
            {
                std::unique_lock lock(mutex);
                ready.wait(lock, [this] { return stopping.load() || input.has_value(); });
                if (stopping.load())
                    return;
                task = std::move(*input);
                input.reset();
            }
            try {
                if (task.snapshot->qpc_frequency != identity.qpc_frequency)
                    throw wire::Error("capture clock changed");
                auto packet = std::make_shared<Packet>();
                packet->bytes = encode_snapshot(*task.snapshot, identity.instance, identity.clock_domain);
                validate_snapshot_size(identity, packet->bytes.size());
                packet->token = task.token;
                packet->sequence = task.snapshot->sequence;
                packet->capture_hz = task.snapshot->capture_target_hz;
                packet->finished_qpc = task.snapshot->interval.finished_qpc;
                {
                    std::lock_guard lock(mutex);
                    if (stopping.load() || state.token != task.token ||
                        state.status.availability != Availability::Ready)
                        continue;
                    state.packet = std::move(packet);
                    if (last_error.starts_with("Encoding:"))
                        last_error.clear();
                    ++encoded;
                }
                wake.notify();
            } catch (const std::exception& error) {
                failure(std::string("Encoding: ") + error.what());
                // 仅当前有效代次的编码错误可以撤销当前来源。
                {
                    std::lock_guard lock(mutex);
                    if (state.token != task.token)
                        continue;
                    ++state.token;
                    ++state.status_revision;
                    ++state.invalidation_serial;
                    state.status.availability = Availability::Unavailable;
                    state.status.reason = "ENCODING_FAILED";
                    state.packet.reset();
                    input.reset();
                }
                wake.notify();
            }
        }
    }
    void network_loop() {
        constexpr unsigned backoff[] = {100, 200, 500, 1000, 2000, 5000};
        std::size_t attempt = 0;
        while (!stopping.load()) {
            const auto started = Steady::now();
            try {
                transport::TcpServer server(options.port, options.maximum_connections, options.bind_address);
                {
                    std::lock_guard lock(mutex);
                    if (last_error.starts_with("TCP:"))
                        last_error.clear();
                }
                server.run(*this, clock, stopping, wake.value);
                if (stopping.load())
                    return;
            } catch (const std::exception& error) {
                failure(std::string("TCP: ") + error.what());
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
    void connected(transport::ConnectionId id) override {
        auto& client = clients.try_emplace(id).first->second;
        client.invalidations = view().invalidation_serial;
        auto info = identity;
        info.capture_hz = frequency.current().hz;
        client.enqueue(hello(info));
        ++connected_count;
    }
    void disconnected(transport::ConnectionId id) noexcept override {
        if (clients.erase(id)) {
            --connected_count;
            try {
                update_demand();
            } catch (...) {
            } // 网络清理不得抛出，采集仍能继续。
        }
    }
    void update_demand() {
        double maximum = 0;
        for (const auto& [id, client] : clients) {
            (void)id;
            if (client.subscribed && !client.follows_capture)
                maximum = std::max(maximum, client.publish_hz);
        }
        frequency.set(maximum > 0 ? maximum : options.capture_hz);
    }
    void received(transport::ConnectionId id, std::span<const std::byte> body, std::uint64_t received) override {
        auto request = decode_request(body, identity.instance);
        auto& client = clients.at(id);
        if (request.request_id <= client.last_request) {
            client.enqueue(
                reject(request.request_id, "STALE_REQUEST", "Request IDs must increase within a connection"));
            return;
        }
        client.last_request = request.request_id;
        if (request.kind == ClientRequest::Kind::ClockQuery) {
            client.enqueue(clock_sample(request.request_id, received, identity));
            return;
        }
        if (request.version != 1) {
            client.enqueue(reject(request.request_id, "VERSION_MISMATCH", OBF("Only protocol v1 is supported")));
            return;
        }
        if (!std::equal(request.schema.begin(), request.schema.end(), cs2_reader::v1::schema_sha256.begin(),
                        cs2_reader::v1::schema_sha256.end())) {
            client.enqueue(reject(request.request_id, "SCHEMA_MISMATCH", "Use the frozen schema descriptor"));
            return;
        }
        const auto hz = request.maximum_publish_hz.value_or(options.capture_hz);
        if (!std::isfinite(hz) || hz < 1 || hz > 300) {
            client.enqueue(reject(request.request_id, "UNSUPPORTED_RATE", "Publish request must be within 1..300 Hz"));
            return;
        }
        const bool replacement = client.subscribed;
        client.subscribed = true;
        client.follows_capture = !request.maximum_publish_hz.has_value();
        client.publish_hz = hz;
        client.pending.reset();
        client.next_publish = {};
        update_demand();
        const auto current = view();
        if (replacement) {
            client.last_published = std::max(client.last_published, current.status.last_success.value_or(0));
            client.minimum_finished_qpc = received;
        }
        const auto capture_hz = frequency.current().hz;
        client.enqueue(subscribed(request.request_id, capture_hz, client.follows_capture ? capture_hz : hz));
        if (!replacement) {
            client.enqueue(status(current.status));
            client.status_revision = current.status_revision;
            client.invalidations = current.invalidation_serial;
        }
    }
    void poll(transport::ConnectionId id) override {
        auto& client = clients.at(id);
        if (!client.subscribed)
            return;
        const auto current = view();
        if (client.status_revision != current.status_revision) {
            client.pending.reset();
            if (client.invalidations != current.invalidation_serial &&
                current.status.availability == Availability::Ready) {
                auto lost = current.status;
                lost.availability = Availability::Unavailable;
                lost.reason = "SOURCE_INVALIDATED_AND_RECOVERED";
                client.enqueue(status(lost));
            }
            client.enqueue(status(current.status));
            client.status_revision = current.status_revision;
            client.invalidations = current.invalidation_serial;
        }
        if (current.status.availability != Availability::Ready) {
            client.pending.reset();
            return;
        }
        if (current.packet && current.packet->finished_qpc >= client.minimum_finished_qpc &&
            current.packet->sequence > client.last_published &&
            (!client.pending || client.pending->sequence != current.packet->sequence)) {
            if (client.pending) {
                ++client.coalesced;
                ++pending_coalesced;
            }
            client.pending = current.packet;
        }
    }
    std::optional<transport::Record> next(transport::ConnectionId id) override {
        auto& client = clients.at(id);
        if (!client.controls.empty()) {
            auto message = std::move(client.controls.front());
            client.controls.pop_front();
            return envelope(identity, ++client.event_sequence, message);
        }
        const auto now = Steady::now();
        const bool every_capture = client.follows_capture || (client.pending && client.pending->capture_hz > 0 &&
                                                              client.publish_hz >= client.pending->capture_hz);
        if (client.subscribed && client.pending && (every_capture || now >= client.next_publish)) {
            const auto current = view();
            // 新采样已交接而编码尚未结束时，不在这个微小窗口抢发上轮候选。
            if (current.status.last_success && client.pending->sequence < *current.status.last_success)
                return {};
            const auto packet = std::move(client.pending);
            client.last_published = packet->sequence;
            const auto period =
                std::chrono::duration_cast<Steady::duration>(std::chrono::duration<double>(1 / client.publish_hz));
            if (client.next_publish == Steady::time_point{})
                client.next_publish = now;
            client.next_publish += period;
            if (client.next_publish <= now)
                client.next_publish += period * ((now - client.next_publish) / period + 1);
            auto record = envelope(identity, ++client.event_sequence, Message{13, packet->bytes, {}});
            record.source_token = packet->token;
            return record;
        }
        if (now - client.last_sent >= std::chrono::seconds(1))
            return envelope(identity, ++client.event_sequence, heartbeat(view().status.last_success, client.coalesced));
        return {};
    }
    bool valid(const transport::Record& record) override {
        if (!record.source_token)
            return true;
        const auto current = view();
        return current.status.availability == Availability::Ready && current.token == *record.source_token;
    }
    void sent(transport::ConnectionId id, const transport::Record& record) override {
        clients.at(id).last_sent = Steady::now();
        if (record.source_token)
            ++sent_count;
    }
    ServiceStats stats() const {
        std::lock_guard lock(mutex);
        return {encoded.load(),           sent_count.load(),      pending_coalesced.load(),
                encoder_coalesced.load(), connected_count.load(), last_error};
    }
};
SubscriptionService::SubscriptionService(const ServiceOptions& options, const capture::IClock& clock,
                                         capture::FrequencyControl* frequency)
    : impl_(std::make_unique<Impl>(options, clock, frequency)) {}
SubscriptionService::~SubscriptionService() = default;
void SubscriptionService::submit(std::shared_ptr<const model::Snapshot> value) {
    impl_->submit(std::move(value));
}
void SubscriptionService::unavailable(Availability value, std::string reason, std::optional<std::uint64_t> epoch) {
    impl_->invalidate(value, std::move(reason), epoch);
}
ServiceStats SubscriptionService::stats() const {
    return impl_->stats();
}
} // namespace cs2::synchronization::v1
