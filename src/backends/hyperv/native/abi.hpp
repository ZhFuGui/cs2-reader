#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace cs2::backends::hyperv::native::abi {
// 对照 hyperv-guest-memory/shared/hypercall.hpp；只声明 Reader 使用的协议子集。
// 数值必须显式固定，不能删除未使用的操作后依赖枚举自动编号。
inline constexpr std::uint64_t allowed_key = 0x8A3B55C7927ULL;
inline constexpr std::uint64_t test_response = 0x1337;
enum class Operation : std::uint16_t {
    Test = 0, CurrentCr3 = 1, PhysicalCopy = 2, VirtualCopy = 3, TranslateVa = 4, ReadBatch = 11
};

inline constexpr std::size_t batch_max_items = 64, batch_max_bytes = 64 * 1024;
struct BatchItem {
    std::uint64_t source_va, destination_va, size;
};
static_assert(std::is_standard_layout_v<BatchItem> && std::is_trivially_copyable_v<BatchItem>);
static_assert(sizeof(BatchItem) == 24 && alignof(BatchItem) == 8);
static_assert(offsetof(BatchItem, source_va) == 0 && offsetof(BatchItem, destination_va) == 8 &&
              offsetof(BatchItem, size) == 16);

struct Request {
    std::uint64_t info;
    std::array<std::uint64_t, 5> args{};
};
[[nodiscard]] constexpr Request request(Operation operation, std::uint64_t a0 = 0, std::uint64_t a1 = 0,
                                        std::uint64_t a2 = 0, std::uint64_t a3 = 0, std::uint64_t a4 = 0) noexcept {
    return {(allowed_key << 16) | static_cast<std::uint16_t>(operation), {a0, a1, a2, a3, a4}};
}
static_assert(allowed_key < (1ULL << 48));
static_assert(std::is_standard_layout_v<Request> && std::is_trivially_copyable_v<Request>);
static_assert(sizeof(Request) == 48 && alignof(Request) == 8);
static_assert(offsetof(Request, info) == 0 && offsetof(Request, args) == 8);
} // namespace cs2::backends::hyperv::native::abi
