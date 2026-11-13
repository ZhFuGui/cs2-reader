#include "synchronization/v1/subscribers.hpp"
#include "cs2_reader/v1/schema.hpp"
#include "diagnostics/obf.hpp"
#include <algorithm>
#include <cmath>

namespace cs2::synchronization::v1 {
Subscribers::Subscribers(const ServerIdentity& identity, Publication& publication, capture::FrequencyControl& frequency,
                         double default_hz)
    : identity_(identity), publication_(publication), frequency_(frequency), default_hz_(default_hz) {}
void Subscribers::Client::enqueue(Message message) {
    if (controls.size() >= 16)
        throw std::runtime_error("control queue resource limit");
    controls.push_back(std::move(message));
}
void Subscribers::connected(transport::ConnectionId id) {
    const auto [position, inserted] = clients_.try_emplace(id);
    if (!inserted)
        throw std::logic_error("Connection identity is already registered");
    ++connected_count_; // 后续构造控制消息失败时，disconnected 仍能准确回收计数。
    auto& client = position->second;
    client.invalidations = publication_.view().invalidation_serial;
    auto info = identity_;
    info.capture_hz = frequency_.current().hz;
    client.enqueue(hello(info));
}
void Subscribers::disconnected(transport::ConnectionId id) noexcept {
    if (clients_.erase(id)) {
        --connected_count_;
        try {
            update_demand();
        } catch (...) {
        } // 网络清理不得抛出，采集仍能继续。
    }
}
void Subscribers::update_demand() {
    double maximum = 0;
    for (const auto& [id, client] : clients_) {
        (void)id;
        if (client.subscribed && !client.follows_capture)
            maximum = std::max(maximum, client.publish_hz);
    }
    frequency_.set(maximum > 0 ? maximum : default_hz_);
}
void Subscribers::received(transport::ConnectionId id, std::span<const std::byte> body, std::uint64_t received) {
    auto request = decode_request(body, identity_.instance);
    auto& client = clients_.at(id);
    if (request.request_id <= client.last_request) {
        client.enqueue(reject(request.request_id, "STALE_REQUEST", "Request IDs must increase within a connection"));
        return;
    }
    client.last_request = request.request_id;
    if (request.kind == ClientRequest::Kind::ClockQuery) {
        client.enqueue(clock_sample(request.request_id, received, identity_));
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
    const auto hz = request.maximum_publish_hz.value_or(default_hz_);
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
    const auto current = publication_.view();
    if (replacement) {
        client.last_published = std::max(client.last_published, current.status.last_success.value_or(0));
        client.minimum_finished_qpc = received;
    }
    const auto capture_hz = frequency_.current().hz;
    client.enqueue(subscribed(request.request_id, capture_hz, client.follows_capture ? capture_hz : hz));
    if (!replacement) {
        client.enqueue(status(current.status));
        client.status_revision = current.status_revision;
        client.invalidations = current.invalidation_serial;
    }
}
void Subscribers::poll(transport::ConnectionId id) {
    auto& client = clients_.at(id);
    if (!client.subscribed)
        return;
    const auto current = publication_.view();
    if (client.status_revision != current.status_revision) {
        client.pending.reset();
        if (client.invalidations != current.invalidation_serial && current.status.availability == Availability::Ready) {
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
            ++pending_coalesced_;
        }
        client.pending = current.packet;
    }
}
std::optional<transport::Record> Subscribers::next(transport::ConnectionId id) {
    auto& client = clients_.at(id);
    if (!client.controls.empty()) {
        auto message = std::move(client.controls.front());
        client.controls.pop_front();
        return envelope(identity_, ++client.event_sequence, message);
    }
    const auto now = Steady::now();
    const bool every_capture = client.follows_capture || (client.pending && client.pending->capture_hz > 0 &&
                                                          client.publish_hz >= client.pending->capture_hz);
    if (client.subscribed && client.pending && (every_capture || now >= client.next_publish)) {
        const auto current = publication_.view();
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
        auto record = envelope(identity_, ++client.event_sequence, Message{13, packet->bytes, {}});
        record.source_token = packet->token;
        return record;
    }
    if (now - client.last_sent >= std::chrono::seconds(1))
        return envelope(identity_, ++client.event_sequence,
                        heartbeat(publication_.view().status.last_success, client.coalesced));
    return {};
}
bool Subscribers::valid(const transport::Record& record) {
    if (!record.source_token)
        return true;
    const auto current = publication_.view();
    return current.status.availability == Availability::Ready && current.token == *record.source_token;
}
void Subscribers::sent(transport::ConnectionId id, const transport::Record& record) {
    clients_.at(id).last_sent = Steady::now();
    if (record.source_token)
        ++sent_count_;
}
SubscriberStats Subscribers::stats() const {
    return {sent_count_.load(), pending_coalesced_.load(), connected_count_.load()};
}
} // namespace cs2::synchronization::v1
