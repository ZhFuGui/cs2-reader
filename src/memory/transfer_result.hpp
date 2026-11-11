#pragma once
#include <cstddef>
#include <cstdint>

namespace cs2::memory {
using Address = std::uint64_t;
enum class TransferStatus { Complete, Partial, InvalidRange, AccessDenied, Unavailable, Unsupported, BackendFailure };
struct TransferResult {
    TransferStatus status = TransferStatus::Unavailable;
    std::size_t transferred = 0;
    std::uint32_t native_status = 0;
    [[nodiscard]] bool complete(std::size_t requested) const noexcept {
        return status == TransferStatus::Complete && transferred == requested;
    }
};
[[nodiscard]] constexpr bool user_range(Address address, std::size_t size = 1) noexcept {
    constexpr Address last = 0x00007FFFFFFFFFFFULL;
    return address >= 0x10000 && address <= last && (!size || size - 1 <= last - address);
}
} // namespace cs2::memory
