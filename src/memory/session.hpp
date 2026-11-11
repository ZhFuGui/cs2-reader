#pragma once
#include "memory/read_channel.hpp"
#include "memory/write_channel.hpp"
#include <memory>
#include <string>
#include <vector>

namespace cs2::memory {
struct ProcessIdentity {
    std::uint32_t pid = 0;
    std::uint64_t creation_time = 0;
    bool operator==(const ProcessIdentity&) const = default;
};
struct Module {
    std::u16string name;
    Address base = 0;
    std::uint32_t size = 0, timestamp = 0;
};
enum class AccessMode { ReadOnly, ReadWrite };
class ISession {
  public:
    virtual ~ISession() = default;
    virtual ProcessIdentity identity() const noexcept = 0;
    virtual Address peb() const noexcept = 0;
    virtual bool alive() const = 0;
    virtual std::unique_ptr<IReadChannel> open_reader() const = 0;
    // 只读会话返回 nullptr；采样器不会获得写入通道。
    virtual std::unique_ptr<IWriteChannel> open_writer() const = 0;
};
std::vector<Module> read_modules(IReadChannel& channel, Address peb);
} // namespace cs2::memory
