#pragma once
#include "capture/clock.hpp"
#include "transport/record.hpp"
#include <atomic>
#include <memory>
#include <span>
#include <string_view>

namespace cs2::transport {
using ConnectionId = std::uint64_t;
class IRecordHandler {
  public:
    virtual ~IRecordHandler() = default;
    virtual void connected(ConnectionId) = 0;
    virtual void received(ConnectionId, std::span<const std::byte>, std::uint64_t received_qpc) = 0;
    virtual void poll(ConnectionId) = 0;
    virtual std::optional<Record> next(ConnectionId) = 0;
    virtual bool valid(const Record&) = 0;
    virtual void sent(ConnectionId, const Record&) = 0;
    virtual void disconnected(ConnectionId) noexcept = 0;
};
// 只处理TCP记录与公平的非阻塞I/O，不识别游戏或protobuf。
class TcpServer final {
  public:
    TcpServer(std::uint16_t port, std::size_t maximum_connections, std::string_view bind_address = "127.0.0.1");
    ~TcpServer();
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    void run(IRecordHandler&, const capture::IClock&, const std::atomic_bool& stopping, void* wake_event);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace cs2::transport
