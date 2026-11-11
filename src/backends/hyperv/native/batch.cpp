#include "backends/hyperv/native/batch.hpp"
#include "memory/transfer_result.hpp"
#include <array>
#include <cstring>

extern "C" std::uint64_t cs2_hypercall(const cs2::backends::hyperv::native::abi::Request* request);
namespace cs2::backends::hyperv::native {
namespace {
bool range(std::uint64_t address, std::uint64_t size) noexcept {
    if (!size)
        return true;
    const auto top = address >> 47;
    return address && size - 1 <= UINT64_MAX - address && (top == 0 || top == 0x1FFFF) &&
           ((address + size - 1) >> 47) == top;
}
bool overlaps(std::uint64_t a, std::uint64_t na, std::uint64_t b, std::uint64_t nb) noexcept {
    return na && nb && (a <= b ? b - a < na : a - b < nb);
}
} // namespace
bool read_batch(std::uint64_t source_cr3, std::span<const abi::BatchItem> items, std::uint64_t& success_mask,
                ReadCounters* counters) noexcept {
    const auto controls = reinterpret_cast<std::uint64_t>(items.data());
    const auto mask_address = reinterpret_cast<std::uint64_t>(&success_mask);
    // 控制区重叠时连初始化 mask 都可能破坏描述符，故必须先拒绝。
    if (items.empty() || items.size() > abi::batch_max_items || !memory::user_range(controls, items.size_bytes()) ||
        !memory::user_range(mask_address, sizeof(success_mask)) || (mask_address & 7) ||
        overlaps(controls, items.size_bytes(), mask_address, sizeof(success_mask)))
        return false;
    success_mask = 0;
    if (!(source_cr3 & 0x0000FFFFFFFFF000ULL) || (source_cr3 >> 48))
        return false;
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        if (item.size > abi::batch_max_bytes - total || !range(item.source_va, item.size) ||
            (item.size && !memory::user_range(item.destination_va, static_cast<std::size_t>(item.size))) ||
            overlaps(item.destination_va, item.size, controls, items.size_bytes()) ||
            overlaps(item.destination_va, item.size, mask_address, sizeof(success_mask)))
            return false;
        total += item.size;
        for (std::size_t j = 0; j < i; ++j)
            if (overlaps(item.destination_va, item.size, items[j].destination_va, items[j].size))
                return false;
    }
    for (const auto& item : items) {
        if (!item.size)
            continue;
        auto* output = reinterpret_cast<volatile unsigned char*>(item.destination_va);
        output[0] = 0;
        for (std::size_t offset = 4096 - (item.destination_va & 4095); offset < item.size; offset += 4096)
            output[offset] = 0;
    }
    const auto request = abi::request(abi::Operation::ReadBatch, source_cr3, controls, items.size(), mask_address);
    if (counters) {
        ++counters->batch;
        counters->items += items.size();
    }
    const auto valid_bits = items.size() == 64 ? UINT64_MAX : (1ULL << items.size()) - 1;
    const bool published = cs2_hypercall(&request) == 1 && !(success_mask & ~valid_bits);
    if (!published) {
        success_mask = 0;
        if (counters)
            ++counters->errors;
    }
    for (std::size_t i = 0; i < items.size(); ++i)
        if (!(success_mask & (1ULL << i)) && items[i].size)
            std::memset(reinterpret_cast<void*>(items[i].destination_va), 0, static_cast<std::size_t>(items[i].size));
    return published;
}
bool probe_batch(std::uint64_t caller_cr3) noexcept {
    const std::array<std::uint64_t, 2> expected{0x5893EB7A2496D10FULL, 0xEAB4958D1237C6F0ULL};
    std::array<std::uint64_t, 2> observed{};
    const std::array<abi::BatchItem, 2> items{
        {{reinterpret_cast<std::uint64_t>(&expected[0]), reinterpret_cast<std::uint64_t>(&observed[0]), 8},
         {reinterpret_cast<std::uint64_t>(&expected[1]), reinterpret_cast<std::uint64_t>(&observed[1]), 8}}};
    alignas(8) std::uint64_t mask = 0;
    return read_batch(caller_cr3, items, mask) && mask == 3 && observed == expected;
}
} // namespace cs2::backends::hyperv::native
