#pragma once
#include "capture/frequency_control.hpp"
#include "synchronization/v1/protocol.hpp"
#include "synchronization/v1/publication.hpp"
#include "transport/record_handler.hpp"
#include <atomic>
#include <chrono>
#include <deque>
#include <map>

namespace cs2::synchronization::v1 {
struct SubscriberStats {
    std::uint64_t sent{}, coalesced{}, connected{};
};
// 网络线程独占连接状态；只处理订阅规则，不创建线程、等待 socket 或读取游戏内存。
class Subscribers final : public transport::IRecordHandler {
  public:
    Subscribers(const ServerIdentity&, Publication&, capture::FrequencyControl&, double default_hz);
    void connected(transport::ConnectionId) override;
    void disconnected(transport::ConnectionId) noexcept override;
    void received(transport::ConnectionId, std::span<const std::byte>, std::uint64_t received_qpc) override;
    void poll(transport::ConnectionId) override;
    std::optional<transport::Record> next(transport::ConnectionId) override;
    bool valid(const transport::Record&) override;
    void sent(transport::ConnectionId, const transport::Record&) override;
    SubscriberStats stats() const;

  private:
    using Steady = std::chrono::steady_clock;
    struct Client {
        bool subscribed = false, follows_capture = false;
        double publish_hz{};
        std::uint64_t event_sequence{}, last_request{}, last_published{}, status_revision{}, invalidations{},
            coalesced{};
        std::uint64_t minimum_finished_qpc{};
        std::deque<Message> controls;
        std::shared_ptr<const EncodedSnapshot> pending;
        Steady::time_point next_publish{}, last_sent = Steady::now();
        void enqueue(Message);
    };
    void update_demand();
    const ServerIdentity& identity_;
    Publication& publication_;
    capture::FrequencyControl& frequency_;
    const double default_hz_;
    std::map<transport::ConnectionId, Client> clients_;
    std::atomic_uint64_t sent_count_{}, pending_coalesced_{}, connected_count_{};
};
} // namespace cs2::synchronization::v1
