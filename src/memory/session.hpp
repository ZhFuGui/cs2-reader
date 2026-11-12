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
    // 记录映像自身格式；进程内的资源映像不必与目标代码模块具有相同架构。
    std::uint16_t machine = 0, optional_magic = 0;
    [[nodiscard]] bool is_x64() const noexcept { return machine == 0x8664 && optional_magic == 0x20B; }
};
enum class AccessMode { ReadOnly, ReadWrite };
enum class ProcessState { Current, Exited, Changed, Unreadable };
class ISession {
  public:
    virtual ~ISession() = default;
    virtual ProcessIdentity identity() const noexcept = 0;
    virtual Address peb() const noexcept = 0;
    virtual ProcessState state() const = 0;
    bool alive() const { return state() == ProcessState::Current; }
    virtual std::unique_ptr<IReadChannel> open_reader() const = 0;
    // 只读会话返回 nullptr；采样器不会获得写入通道。
    virtual std::unique_ptr<IWriteChannel> open_writer() const = 0;
};
std::vector<Module> read_modules(IReadChannel& channel, Address peb);
} // namespace cs2::memory
