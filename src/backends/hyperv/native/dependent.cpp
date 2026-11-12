#include "backends/hyperv/native/dependent.hpp"
#include "memory/dependent_read.hpp"
#include <array>
#include <cstring>

extern "C" std::uint64_t cs2_hypercall(const cs2::backends::hyperv::native::abi::Request*);
namespace cs2::backends::hyperv::native {
namespace {
bool overlaps(std::uint64_t a, std::size_t na, std::uint64_t b, std::size_t nb) noexcept {
    return na && nb && (a <= b ? b - a < na : a - b < nb);
}
void prepare(void* address, std::size_t bytes) noexcept {
    auto* output = static_cast<volatile unsigned char*>(address);
    output[0] = 0;
    for (std::size_t offset = 4096 - (reinterpret_cast<std::uintptr_t>(address) & 4095); offset < bytes; offset += 4096)
        output[offset] = 0;
}
} // namespace
bool read_dependent(std::uint64_t source_cr3, std::span<const abi::DependentItem> items, abi::DependentResult& result,
                    ReadCounters* counters) noexcept {
    const auto controls = reinterpret_cast<std::uint64_t>(items.data());
    const auto output = reinterpret_cast<std::uint64_t>(&result);
    if (items.empty() || items.size() > abi::dependent_max_items || !memory::user_range(controls, items.size_bytes()) ||
        !memory::user_range(output, sizeof(result)) || overlaps(controls, items.size_bytes(), output, sizeof(result)))
        return false;
    std::size_t bytes = 0;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        if (item.reserved || !item.size || item.size > abi::dependent_max_bytes - bytes ||
            (item.parent == abi::dependent_direct
                 ? !memory::canonical_range(item.address, item.size)
                 : item.parent >= i || items[item.parent].kind != abi::DependentKind::Pointer))
            return false;
        bytes += item.size;
        if (item.kind == abi::DependentKind::Pointer) {
            if (item.size != 8 || item.destination_va)
                return false;
        } else if (item.kind == abi::DependentKind::Copy) {
            if (!memory::user_range(item.destination_va, item.size) ||
                overlaps(item.destination_va, item.size, controls, items.size_bytes()) ||
                overlaps(item.destination_va, item.size, output, sizeof(result)))
                return false;
            for (std::size_t j = 0; j < i; ++j)
                if (items[j].kind == abi::DependentKind::Copy &&
                    overlaps(item.destination_va, item.size, items[j].destination_va, items[j].size))
                    return false;
        } else
            return false;
    }
    result = {};
    if (!(source_cr3 & 0x0000FFFFFFFFF000ULL) || (source_cr3 >> 48))
        return false;
    // 控制数组由本进程持有；触碰全部页，输出也提前建立可写驻留页。
    const auto* input = reinterpret_cast<const volatile unsigned char*>(items.data());
    (void)input[0];
    for (std::size_t offset = 4096 - (controls & 4095); offset < items.size_bytes(); offset += 4096)
        (void)input[offset];
    prepare(&result, sizeof(result));
    for (const auto& item : items)
        if (item.kind == abi::DependentKind::Copy)
            prepare(reinterpret_cast<void*>(item.destination_va), item.size);
    const auto request = abi::request(abi::Operation::ReadDependent, source_cr3, controls, items.size(), output);
    if (counters) {
        ++counters->dependent;
        counters->dependent_items += items.size();
    }
    bool published = cs2_hypercall(&request) == 1;
    for (std::size_t i = 0; published && i < abi::dependent_max_items; ++i) {
        const auto status = result.status[i];
        if (i >= items.size()) {
            published = status == abi::DependentStatus::Unused && !result.pointers[i];
            continue;
        }
        const auto& item = items[i];
        const bool blocked =
            item.parent != abi::dependent_direct && result.status[item.parent] != abi::DependentStatus::Success;
        published = status >= abi::DependentStatus::Success && status <= abi::DependentStatus::DependencyFailed &&
                    (blocked == (status == abi::DependentStatus::DependencyFailed));
        if (item.kind == abi::DependentKind::Pointer && status == abi::DependentStatus::Success)
            published = published && memory::canonical_range(result.pointers[i], 1);
        else
            published = published && !result.pointers[i];
    }
    if (!published) {
        result = {};
        if (counters)
            ++counters->errors;
    }
    for (std::size_t i = 0; i < items.size(); ++i)
        if (items[i].kind == abi::DependentKind::Copy && result.status[i] != abi::DependentStatus::Success)
            std::memset(reinterpret_cast<void*>(items[i].destination_va), 0, items[i].size);
    return published;
}
bool probe_dependent(std::uint64_t caller_cr3) noexcept {
    struct Node {
        std::uint64_t next, value;
    };
    Node leaf{0, 0x5893EB7A2496D10FULL};
    Node root{reinterpret_cast<std::uint64_t>(&leaf), 0xEAB4958D1237C6F0ULL};
    const auto pointer = reinterpret_cast<std::uint64_t>(&root);
    const std::uint64_t null = 0;
    std::array<std::uint64_t, 3> observed{};
    using K = abi::DependentKind;
    const std::array<abi::DependentItem, 6> items{
        {{reinterpret_cast<std::uint64_t>(&pointer), 0, 8, abi::dependent_direct, K::Pointer, 0},
         {0, 0, 8, 0, K::Pointer, 0},
         {8, reinterpret_cast<std::uint64_t>(&observed[0]), 8, 1, K::Copy, 0},
         {8, reinterpret_cast<std::uint64_t>(&observed[1]), 8, 0, K::Copy, 0},
         {reinterpret_cast<std::uint64_t>(&null), 0, 8, abi::dependent_direct, K::Pointer, 0},
         {0, reinterpret_cast<std::uint64_t>(&observed[2]), 8, 4, K::Copy, 0}}};
    abi::DependentResult result{};
    return read_dependent(caller_cr3, items, result) && observed[0] == leaf.value && observed[1] == root.value &&
           !observed[2] && result.pointers[0] == pointer && result.pointers[1] == root.next &&
           result.status[4] == abi::DependentStatus::InvalidAddress &&
           result.status[5] == abi::DependentStatus::DependencyFailed;
}
} // namespace cs2::backends::hyperv::native
