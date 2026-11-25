#include "synchronization/v2/subscribers.hpp"
#include "cs2_reader/v2/schema.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cs2::synchronization::v2 {
Subscribers::Subscribers(const ServerIdentity& identity, Publication& publication, capture::FrequencyControl& frequency,
                         double default_hz, const capture::IClock& clock)
    : identity_(identity), publication_(publication), frequency_(frequency), default_hz_(default_hz), clock_(clock) {}
void Subscribers::attach(tcp::TcpServer* server) {
    std::lock_guard lock(connections_mutex_);
    server_ = server;
}
void Subscribers::notify() {
    std::lock_guard lock(connections_mutex_);
    ++notification_revision_;
    if (!server_)
        return;
    server_->notify();
    const auto now = clock_.now();
    for (const auto& [id, client] : clients_) {
        (void)client;
        server_->schedule(id, now);
    }
}
void Subscribers::Client::enqueue(Message message) {
    if (controls.size() >= 16)
        throw std::runtime_error("control queue resource limit");
    controls.push_back(std::move(message));
}
void Subscribers::connected(const tcp::PeerInfo& peer) {
    std::unique_lock lock(connections_mutex_);
    const auto [position, inserted] = clients_.try_emplace(peer.id);
    lock.unlock();
    if (!inserted)
        throw std::logic_error("Connection identity is already registered");
    ++connected_count_; // 后续构造控制消息失败时，disconnected 仍能准确回收计数。
    auto& client = position->second;
    client.last_sent = clock_.now();
    client.invalidations = publication_.view().invalidation_serial;
    auto info = identity_;
    info.capture_hz = frequency_.current().hz;
    client.enqueue(hello(info));
    pump(peer.id);
}
void Subscribers::disconnected(tcp::ConnectionId id, tcp::Error) noexcept {
    try {
        std::unique_lock lock(connections_mutex_);
        const auto removed = clients_.erase(id);
        lock.unlock();
        if (removed) {
            --connected_count_;
            update_demand();
        } // 网络清理不得抛出，采集仍能继续。
    } catch (...) {
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
void Subscribers::received(tcp::ConnectionId id, std::span<const std::byte> body, std::uint64_t received) {
    request(id, body, received);
    pump(id);
}
void Subscribers::request(tcp::ConnectionId id, std::span<const std::byte> body, std::uint64_t received) {
    auto request = decode_request(body, identity_.instance);
    auto& client = clients_.at(id);
    if (request.request_id <= client.last_request) {
        client.enqueue(reject(request.request_id, ReasonCode::StaleRequest));
        return;
    }
    client.last_request = request.request_id;
    if (request.kind == ClientRequest::Kind::ClockQuery) {
        client.enqueue(clock_sample(request.request_id, received, identity_));
        return;
    }
    if (request.version != cs2_reader::v2::protocol_version) {
        client.enqueue(reject(request.request_id, ReasonCode::ProtocolVersionMismatch));
        return;
    }
    if (!std::equal(request.schema.begin(), request.schema.end(), cs2_reader::v2::schema_sha256.begin(),
                    cs2_reader::v2::schema_sha256.end())) {
        client.enqueue(reject(request.request_id, ReasonCode::SchemaMismatch));
        return;
    }
    const auto hz = request.maximum_publish_hz.value_or(default_hz_);
    if (!std::isfinite(hz) || hz < 1 || hz > 300) {
        client.enqueue(reject(request.request_id, ReasonCode::UnsupportedRate));
        return;
    }
    const bool replacement = client.subscribed;
    client.subscribed = true;
    client.follows_capture = !request.maximum_publish_hz.has_value();
    client.publish_hz = hz;
    client.pending.reset();
    client.next_publish = {};
    ++client.subscription_revision;
    if (replacement)
        client.minimum_finished_qpc = received;
    update_demand();
    const auto capture_hz = frequency_.current().hz;
    client.enqueue(subscribed(request.request_id, capture_hz, client.follows_capture ? capture_hz : hz));
    if (!replacement) {
        const auto current = publication_.view();
        client.enqueue(status(current.status));
        client.status_revision = current.status_revision;
        client.invalidations = current.invalidation_serial;
    }
}
void Subscribers::poll(Client& client) {
    if (!client.subscribed)
        return;
    const auto current = publication_.view();
    if (client.status_revision != current.status_revision) {
        client.pending.reset();
        if (client.invalidations != current.invalidation_serial && current.status.availability == Availability::Ready) {
            auto lost = current.status;
            lost.availability = Availability::Unavailable;
            lost.reason = ReasonCode::SourceInvalidated;
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
bool Subscribers::submit(tcp::ConnectionId id, Client& client, EncodedRecord encoded,
                         std::optional<std::uint64_t> token) {
    if (client.record_tag == std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("record identity exhausted");
    const auto tag = ++client.record_tag;
    client.records.emplace(tag, RecordMetadata{encoded.sequence_offset, std::move(encoded.stamp_offsets), token,
                                               client.subscription_revision});
    tcp::Record record{std::move(encoded.body), tag, token ? tcp::Delivery::Latest : tcp::Delivery::Reliable,
                       token ? 1ULL : 0ULL};
    const auto result = server_->submit(id, std::move(record));
    if (!result) {
        client.records.erase(tag);
        switch (result.status) {
        case tcp::SubmitStatus::Backpressured:
        case tcp::SubmitStatus::Disconnected:
            break;
        case tcp::SubmitStatus::TooLarge:
        case tcp::SubmitStatus::InvalidArgument:
            server_->close(id, {tcp::ErrorCode::InvalidRecord});
            break;
        case tcp::SubmitStatus::ResourceExhausted:
            server_->close(id, {tcp::ErrorCode::ResourceExhausted});
            break;
        case tcp::SubmitStatus::Unavailable:
            server_->close(id, {tcp::ErrorCode::SocketFailure});
            break;
        case tcp::SubmitStatus::Accepted:
        case tcp::SubmitStatus::Replaced:
            throw std::logic_error("inconsistent submit result");
        }
        return false;
    }
    if (result.replaced) {
        client.records.erase(result.replaced->tag);
        ++client.coalesced;
        ++pending_coalesced_;
    }
    return true;
}
void Subscribers::pump(tcp::ConnectionId id) {
    const auto notification = notification_revision_.load();
    auto& client = clients_.at(id);
    poll(client);
    bool blocked = false;
    const auto controls = [&] {
        while (!client.controls.empty()) {
            if (!submit(id, client, envelope(identity_, 0, client.controls.front()))) {
                blocked = true;
                break;
            }
            client.controls.pop_front();
        }
    };
    controls();
    const auto now = clock_.now();
    const bool every_capture = client.follows_capture || (client.pending && client.pending->capture_hz > 0 &&
                                                          client.publish_hz >= client.pending->capture_hz);
    if (!blocked && client.subscribed && client.pending && (every_capture || now >= client.next_publish)) {
        // 正文已由编码线程完成；此处只复制每连接独占的发送正文，不重新序列化快照。
        const auto& packet = client.pending;
        if (submit(id, client, packet->record, packet->token)) {
            client.last_published = packet->sequence;
            const auto period = std::max<std::uint64_t>(
                1, static_cast<std::uint64_t>(static_cast<double>(identity_.qpc_frequency) / client.publish_hz));
            if (!client.next_publish)
                client.next_publish = now;
            client.next_publish += period;
            if (client.next_publish <= now)
                client.next_publish += period * ((now - client.next_publish) / period + 1);
            client.pending.reset();
        } else
            blocked = true;
    }
    if (!blocked && client.controls.empty() && client.records.empty() &&
        now - client.last_sent >= identity_.qpc_frequency) {
        client.enqueue(heartbeat(publication_.view().status.last_success, client.coalesced));
        controls();
    }
    // 每连接只有一个传输定时器；发布和心跳在这里合并为最近截止时刻。
    std::uint64_t due = 0;
    if (client.pending && !every_capture && client.next_publish > now)
        due = client.next_publish;
    if (!blocked && client.controls.empty() && client.records.empty()) {
        const auto heartbeat_due = client.last_sent + identity_.qpc_frequency;
        due = due ? std::min(due, heartbeat_due) : heartbeat_due;
    }
    std::lock_guard lock(connections_mutex_);
    // 编码提交可能发生在 poll 与重排定时器之间，不能用较晚截止覆盖它的即时唤醒。
    server_->schedule(id, notification != notification_revision_.load() ? clock_.now() : due);
}
bool Subscribers::eligible(tcp::ConnectionId id, const tcp::RecordInfo& record, bool started) {
    const auto& client = clients_.at(id);
    const auto found = client.records.find(record.tag);
    if (found == client.records.end())
        return false;
    const auto& metadata = found->second;
    if (!metadata.source_token)
        return true;
    // 调频只撤销尚未写入 TCP 的快照，已开始的记录仍先于回执完整送达。
    if (!started && metadata.subscription_revision != client.subscription_revision)
        return false;
    const auto current = publication_.view();
    return current.status.availability == Availability::Ready && current.token == *metadata.source_token;
}
bool Subscribers::before_send(tcp::ConnectionId id, const tcp::RecordInfo& record, std::span<std::byte> body,
                              std::uint64_t qpc) {
    auto& client = clients_.at(id);
    const auto& metadata = client.records.at(record.tag);
    if (client.event_sequence == std::numeric_limits<std::uint64_t>::max())
        return false;
    // Reader v2 要求严格递增，允许因未开始记录撤销而留空号；这里不是 sent 或 ACK。
    wire::patch_fixed64(body, metadata.sequence_offset, ++client.event_sequence);
    for (const auto offset : metadata.stamp_offsets)
        wire::patch_fixed64(body, offset, qpc);
    return true;
}
void Subscribers::sent(tcp::ConnectionId id, const tcp::RecordInfo& record, const tcp::SendTiming& timing) {
    auto& client = clients_.at(id);
    const auto found = client.records.find(record.tag);
    if (found == client.records.end())
        throw std::logic_error("missing record metadata");
    client.last_sent = timing.finished_qpc;
    if (found->second.source_token)
        ++sent_count_;
    client.records.erase(found);
    pump(id);
}
void Subscribers::discarded(tcp::ConnectionId id, const tcp::RecordInfo& record, tcp::DiscardReason reason) {
    auto& client = clients_.at(id);
    client.records.erase(record.tag);
    if (reason == tcp::DiscardReason::Superseded) {
        ++client.coalesced;
        ++pending_coalesced_;
    }
    if (reason != tcp::DiscardReason::Closed)
        server_->schedule(id, clock_.now());
}
void Subscribers::writable(tcp::ConnectionId id) {
    pump(id);
}
void Subscribers::timer(tcp::ConnectionId id, std::uint64_t) {
    pump(id);
}
void Subscribers::service_changed(tcp::ServiceStatus status) noexcept {
    try {
        if (status.state == tcp::ServiceState::Listening)
            publication_.network_recovered();
        else if (status.state == tcp::ServiceState::Recovering || status.state == tcp::ServiceState::Failed)
            publication_.network_failed(status.error.native_status ? std::optional(status.error.native_status)
                                                                   : std::nullopt);
    } catch (...) {
    }
}
SubscriberStats Subscribers::stats() const {
    return {sent_count_.load(), pending_coalesced_.load(), connected_count_.load()};
}
} // namespace cs2::synchronization::v2
