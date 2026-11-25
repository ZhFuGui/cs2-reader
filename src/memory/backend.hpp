#pragma once
#include "memory/process_catalog.hpp"
#include "memory/session.hpp"
#include <optional>
namespace cs2::memory {
class IBackend {
  public:
    virtual ~IBackend() = default;
    virtual std::unique_ptr<ISession> attach(ProcessIdentity process, AccessMode access) = 0;
    // 后端提供已核验的进程发现；运行层不依赖具体枚举方式。
    virtual IProcessCatalog& process_catalog() noexcept = 0;
    // 独立核对活动链表中的 PID 实例。确认缺失或退出才返回空，无法完成核对则抛异常。
    virtual std::optional<ProcessIdentity> find_process_identity(std::uint32_t pid) = 0;
};
} // namespace cs2::memory
