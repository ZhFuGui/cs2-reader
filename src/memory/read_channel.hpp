#pragma once
#include "diagnostics/read_metrics.hpp"
#include "memory/dependent_read.hpp"
#include "memory/transfer_result.hpp"
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>

namespace cs2::memory {
using ReadRequest = readers::memory::ReadRequest;
class IReadChannel {
  public:
    virtual ~IReadChannel() = default;
    virtual TransferResult read(Address address, void* destination, std::size_t bytes) noexcept = 0;
    // 同步完成；调用期间请求及互不重叠的目标缓冲必须独占、稳定。
    // 默认实现也委托共享层验证；生产通道直接传递整份计划。
    virtual void read_many(std::span<ReadRequest>) noexcept;
    virtual void read_dependent(std::span<DependentRead>) noexcept;
    virtual diagnostics::ReadMetrics take_backend_metrics() noexcept { return {}; }
};
template <class T> [[nodiscard]] std::optional<T> read_value(IReadChannel& channel, Address address) noexcept {
    static_assert(std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>);
    T scratch{};
    if (!channel.read(address, &scratch, sizeof(T)).complete(sizeof(T)))
        return std::nullopt;
    return scratch;
}
} // namespace cs2::memory
