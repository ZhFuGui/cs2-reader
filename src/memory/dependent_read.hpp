#pragma once
#include "memory/transfer_result.hpp"
#include <optional>
#include <readers/memory/read_plan.hpp>
#include <span>

namespace cs2::memory {
using readers::memory::direct_read;
using ReadKind = readers::memory::ReadKind;
using DependentRead = readers::memory::DependentRead;
// 仅用于产品关闭证明还原实际来源地址，不执行读取或计划解释。
bool canonical_range(Address, std::size_t) noexcept;
std::optional<Address> relative_address(Address, std::uint64_t offset, std::size_t bytes) noexcept;
} // namespace cs2::memory
