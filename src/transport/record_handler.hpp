#pragma once
#include "transport/record.hpp"
#include <span>

namespace cs2::transport {
using ConnectionId = std::uint64_t;
// 协议只接收记录事件；具体连接和非阻塞 I/O 由传输实现拥有。
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
} // namespace cs2::transport
