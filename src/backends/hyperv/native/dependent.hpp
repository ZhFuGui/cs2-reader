#pragma once
#include "backends/hyperv/native/hypercall.hpp"
#include <span>

namespace cs2::backends::hyperv::native {
// true 仅表示整份结果已发布且格式有效；失败复制缓冲会清空，不能解释为零值。
bool read_dependent(std::uint64_t source_cr3, std::span<const abi::DependentItem>, abi::DependentResult&,
                    ReadCounters* = nullptr) noexcept;
bool probe_dependent(std::uint64_t caller_cr3) noexcept;
} // namespace cs2::backends::hyperv::native
