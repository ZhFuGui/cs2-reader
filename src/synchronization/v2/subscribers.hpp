#pragma once
#include "capture/clock.hpp"
#include "capture/frequency_control.hpp"
#include "synchronization/v2/protocol.hpp"
#include "synchronization/v2/publication.hpp"
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <readers/transport/tcp_server.hpp>

namespace cs2::synchronization::v2 {
namespace tcp = readers::transport;
struct SubscriberStats {
    std::uint64_t sent{}, coalesced{}, connected{};
};
// 网络线程独占协议状态。共享层独占 socket、长度组包、发送队列及短写游标。
class Subscribers final : public tcp::RecordHandler {
  public:
    Subscribers(const ServerIdentity&, Publication&, capture::FrequencyControl&, double default_hz,
                const capture::IClock&);
    // 启动前交接，join 完成后撤销；不与回调并发销毁。
    void attach(tcp::TcpServer*);
    void notify();
    void connected(const tcp::PeerInfo&) override;
    void disconnected(tcp::ConnectionId, tcp::Error) noexcept override;
    void received(tcp::ConnectionId, std::span<const std::byte>, std::uint64_t received_qpc) override;
    bool eligible(tcp::ConnectionId, const tcp::RecordInfo&, bool started) override;
    bool before_send(tcp::ConnectionId, const tcp::RecordInfo&, std::span<std::byte>, std::uint64_t) override;
    void sent(tcp::ConnectionId, const tcp::RecordInfo&, const tcp::SendTiming&) override;
    void discarded(tcp::ConnectionId, const tcp::RecordInfo&, tcp::DiscardReason) override;
    void writable(tcp::ConnectionId) override;
    void timer(tcp::ConnectionId, std::uint64_t) override;
    void service_changed(tcp::ServiceStatus) noexcept override;
    SubscriberStats stats() const;

  private:
    struct RecordMetadata {
        std::size_t sequence_offset{};
        std::vector<std::size_t> stamp_offsets;
        std::optional<std::uint64_t> source_token;
        std::uint64_t subscription_revision{};
    };
    struct Client {
        bool subscribed = false, follows_capture = false;
        double publish_hz{};
        std::uint64_t event_sequence{}, record_tag{}, last_request{}, last_published{}, status_revision{},
            invalidations{}, coalesced{};
        std::uint64_t minimum_finished_qpc{}, subscription_revision{}, next_publish{}, last_sent{};
        // 已提交的正文只由共享 TCP 拥有，这里仅保留协议控制结果和最新不可变状态。
        std::deque<Message> controls;
        std::shared_ptr<const EncodedSnapshot> pending;
        std::map<std::uint64_t, RecordMetadata> records;
        void enqueue(Message);
    };
    void request(tcp::ConnectionId, std::span<const std::byte>, std::uint64_t);
    void poll(Client&);
    void pump(tcp::ConnectionId);
    bool submit(tcp::ConnectionId, Client&, EncodedRecord, std::optional<std::uint64_t> source_token = {});
    void update_demand();
    const ServerIdentity& identity_;
    Publication& publication_;
    capture::FrequencyControl& frequency_;
    const double default_hz_;
    const capture::IClock& clock_;
    // 仅保护连接集合结构和服务器交接，不在游戏读取、编码或 socket I/O 期间持有。
    std::mutex connections_mutex_;
    tcp::TcpServer* server_ = nullptr;
    std::map<tcp::ConnectionId, Client> clients_;
    std::atomic_uint64_t sent_count_{}, pending_coalesced_{}, connected_count_{}, notification_revision_{};
};
} // namespace cs2::synchronization::v2
