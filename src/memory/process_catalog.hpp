#pragma once
#include "memory/process_name.hpp"
#include "memory/session.hpp"
#include <optional>

namespace cs2::memory {
// 发现快照与绑定存活证明互相独立；失效后才允许重新枚举。
class IProcessCatalog {
  public:
    virtual ~IProcessCatalog() = default;
    virtual std::optional<ProcessIdentity> find(ProcessNameKey, std::optional<std::uint32_t> selected = {}) = 0;
    virtual void invalidate() = 0;
};
} // namespace cs2::memory
