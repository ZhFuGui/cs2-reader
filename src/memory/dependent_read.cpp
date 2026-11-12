#include "memory/read_channel.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <new>
#include <vector>

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
bool valid_dependent_reads(std::span<const DependentRead> requests) {
    if (requests.empty())
        return true;
    const auto controls = reinterpret_cast<Address>(requests.data());
    if (requests.size() > SIZE_MAX / sizeof(DependentRead) || !user_range(controls, requests.size_bytes()))
        return false;
    std::size_t copies = 0;
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
            ++copies;
        }
    }
    struct Interval {
        Address begin, end;
    };
    // 常规小计划无需堆分配；大计划按目标地址排序，避免逐项两两比较。
    std::array<Interval, 128> local;
    std::vector<Interval> heap;
    auto* outputs = local.data();
    if (copies > local.size()) {
        heap.resize(copies);
        outputs = heap.data();
    }
    std::size_t count = 0;
    for (const auto& r : requests)
        if (r.kind == ReadKind::Copy && r.bytes) {
            const auto begin = reinterpret_cast<Address>(r.destination);
            outputs[count++] = {begin, begin + r.bytes};
        }
    std::sort(outputs, outputs + count, [](const auto& a, const auto& b) { return a.begin < b.begin; });
    for (std::size_t i = 1; i < count; ++i)
        if (outputs[i].begin < outputs[i - 1].end)
            return false;
    return true;
}
void IReadChannel::read_dependent(std::span<DependentRead> requests) noexcept {
    auto initial = TransferStatus::Unavailable;
    try {
        if (!valid_dependent_reads(requests))
            initial = TransferStatus::InvalidRange;
    } catch (const std::bad_alloc&) {
        initial = TransferStatus::BackendFailure;
    }
    for (auto& r : requests) {
        r.pointer = 0;
        r.result = {initial};
    }
    if (initial != TransferStatus::Unavailable)
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
