#pragma once
#include "memory/session.hpp"
#include <optional>
#include <string_view>
namespace cs2::memory {
class IBackend {
  public:
    virtual ~IBackend() = default;
    virtual std::unique_ptr<ISession> attach(ProcessIdentity process, AccessMode access) = 0;
    // 只提供重新发现的线索；原生进程快照负责最终名称/PID/创建时间选择。
    virtual std::optional<ProcessIdentity> find_process_hint(std::string_view image_name) = 0;
};
} // namespace cs2::memory
