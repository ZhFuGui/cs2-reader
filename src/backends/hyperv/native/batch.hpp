#pragma once
#include "backends/hyperv/native/abi.hpp"
#include "backends/hyperv/native/hypercall.hpp"
#include <span>

namespace cs2::backends::hyperv::native {
// true 仅说明位图已发布；仍须逐项检查成功位。已提交批次的失败输出在本地清理。
bool read_batch(std::uint64_t source_cr3, std::span<const abi::BatchItem> items, std::uint64_t& success_mask,
                ReadCounters* counters = nullptr) noexcept;
bool probe_batch(std::uint64_t caller_cr3) noexcept;
} // namespace cs2::backends::hyperv::native
