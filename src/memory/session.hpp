#pragma once
#include "memory/modules.hpp"
#include "memory/read_channel.hpp"
#include <memory>

namespace cs2::memory {
using ProcessIdentity = readers::memory::ProcessIdentity;
enum class AccessMode { ReadOnly, ReadWrite };
// Changed 表示进程实例变化；地址空间变化单独处理，不能误判成固定 PID 的目标退出。
enum class ProcessState { Current, Exited, Changed, Unreadable, AddressSpaceChanged };
class ISession {
  public:
    virtual ~ISession() = default;
    virtual ProcessIdentity identity() const noexcept = 0;
    virtual std::uint64_t binding_id() const noexcept = 0;
    virtual ProcessState state() const = 0;
    virtual std::unique_ptr<ISession> rebind() const = 0;
    virtual std::vector<Module> modules(std::span<const std::u16string_view> names = {}) const = 0;
    bool alive() const { return state() == ProcessState::Current; }
    virtual std::unique_ptr<IReadChannel> open_reader() const = 0;
};
} // namespace cs2::memory
