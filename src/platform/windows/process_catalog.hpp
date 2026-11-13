#pragma once
#include "memory/process_catalog.hpp"
#include <cstddef>
#include <mutex>
#include <vector>

namespace cs2::platform::windows {
// 本机快照只在首次访问或显式失效后刷新；不能把缓存当作存活证明。
class ProcessCatalog final : public memory::IProcessCatalog {
  public:
    void invalidate() override;
    memory::ProcessCatalogStats stats() const override;
    std::optional<memory::ProcessIdentity> find(memory::ProcessNameKey,
                                                std::optional<std::uint32_t> selected = {}) override;
    std::optional<memory::ProcessIdentity> identity(std::uint32_t pid);

  private:
    void grow(std::size_t required);
    void ensure();
    template <class Visitor> void visit(Visitor&&) const;
    mutable std::mutex mutex_;
    std::vector<std::byte> buffer_;
    std::size_t used_ = 0;
    bool valid_ = false;
    memory::ProcessCatalogStats stats_;
};
ProcessCatalog& process_catalog();
} // namespace cs2::platform::windows
