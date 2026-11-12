#include "memory/read_channel.hpp"
#include <cstring>

namespace cs2::memory {
namespace {
bool overlaps(Address a, std::size_t na, Address b, std::size_t nb) noexcept {
    return na && nb && (a <= b ? b - a < na : a - b < nb);
}
} // namespace
bool canonical_range(Address address, std::size_t bytes) noexcept {
    if (!address || (bytes && bytes - 1 > UINT64_MAX - address))
        return false;
    const auto top = address >> 47;
    return (top == 0 || top == 0x1FFFF) && ((address + (bytes ? bytes - 1 : 0)) >> 47) == top;
}
std::optional<Address> relative_address(Address base, std::uint64_t offset, std::size_t bytes) noexcept {
    Address address;
    if (offset >> 63) {
        const auto magnitude = ~offset + 1;
        if (magnitude > base)
            return {};
        address = base - magnitude;
    } else {
        if (offset > UINT64_MAX - base)
            return {};
        address = base + offset;
    }
    return canonical_range(address, bytes) ? std::optional(address) : std::nullopt;
}
bool valid_dependent_reads(std::span<const DependentRead> requests) noexcept {
    const auto controls = reinterpret_cast<Address>(requests.data());
    for (std::size_t i = 0; i < requests.size(); ++i) {
        const auto& r = requests[i];
        if (r.kind != ReadKind::Copy && r.kind != ReadKind::Pointer)
            return false;
        if (r.parent == direct_read) {
            if (!canonical_range(r.address, r.bytes))
                return false;
        } else if (r.parent >= i || requests[r.parent].kind != ReadKind::Pointer)
            return false;
        if (r.kind == ReadKind::Pointer) {
            if (r.bytes != sizeof(Address) || r.destination)
                return false;
        } else if (r.bytes) {
            const auto destination = reinterpret_cast<Address>(r.destination);
            if (!user_range(destination, r.bytes) || overlaps(destination, r.bytes, controls, requests.size_bytes()))
                return false;
            for (std::size_t j = 0; j < i; ++j)
                if (requests[j].kind == ReadKind::Copy &&
                    overlaps(destination, r.bytes, reinterpret_cast<Address>(requests[j].destination),
                             requests[j].bytes))
                    return false;
        }
    }
    return true;
}
void IReadChannel::read_dependent(std::span<DependentRead> requests) noexcept {
    const auto valid = valid_dependent_reads(requests);
    for (auto& r : requests) {
        r.pointer = 0;
        r.result = {valid ? TransferStatus::Unavailable : TransferStatus::InvalidRange};
    }
    if (!valid)
        return;
    for (auto& r : requests) {
        auto address = std::optional(r.address);
        if (r.parent != direct_read) {
            const auto& parent = requests[r.parent];
            if (!parent.result.complete(sizeof(Address))) {
                r.result = {TransferStatus::DependencyFailed};
                continue;
            }
            address = relative_address(parent.pointer, r.address, r.bytes);
        }
        if (!address) {
            r.result = {TransferStatus::InvalidRange};
            continue;
        }
        if (r.kind == ReadKind::Pointer) {
            Address pointer{};
            const auto result = read(*address, &pointer, sizeof(pointer));
            if (!result.complete(sizeof(pointer)))
                r.result = {TransferStatus::Unavailable};
            else if (!canonical_range(pointer, 1))
                r.result = {TransferStatus::InvalidRange};
            else {
                r.pointer = pointer;
                r.result = result;
            }
        } else {
            r.result = r.bytes ? read(*address, r.destination, r.bytes) : TransferResult{TransferStatus::Complete};
        }
    }
    for (auto& r : requests)
        if (r.kind == ReadKind::Copy && r.bytes && !r.result.complete(r.bytes))
            std::memset(r.destination, 0, r.bytes);
}
} // namespace cs2::memory
