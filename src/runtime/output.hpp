#pragma once
#include "model/snapshot.hpp"
#include "model/source_status.hpp"
#include <iosfwd>
#include <memory>

namespace cs2::runtime {
// 所有权交接不包含编码、socket 或磁盘等待；可选输出由组合层装配。
class IOutput {
  public:
    virtual ~IOutput() = default;
    virtual void submit(std::shared_ptr<const model::Snapshot>) = 0;
    virtual void unavailable(model::Availability, std::string reason, std::optional<std::uint64_t> epoch = {}) = 0;
    virtual void report(std::ostream&) const = 0;
    virtual std::optional<std::uint64_t> coalesced() const = 0;
};
} // namespace cs2::runtime
