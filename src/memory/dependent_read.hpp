#pragma once
#include "memory/transfer_result.hpp"
#include <limits>
#include <optional>
#include <span>

namespace cs2::memory {
inline constexpr std::size_t direct_read = std::numeric_limits<std::size_t>::max();
enum class ReadKind { Copy, Pointer };
struct DependentRead {
    // 有 parent 时，address 是 int64_t 字节偏移的二进制表示。
    Address address;
    void* destination;
    std::size_t bytes;
    std::size_t parent = direct_read;
    ReadKind kind = ReadKind::Copy;
    TransferResult result{};
    Address pointer = 0;
};
bool canonical_range(Address, std::size_t) noexcept;
std::optional<Address> relative_address(Address, std::uint64_t offset, std::size_t bytes) noexcept;
// 只检查拓扑、范围和缓冲所有权，不读取目标内存，也不设置 ABI 数量限制。
bool valid_dependent_reads(std::span<const DependentRead>) noexcept;
} // namespace cs2::memory
