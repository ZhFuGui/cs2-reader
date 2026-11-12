#include "backends/hyperv/channel.hpp"
#include "backends/hyperv/native/dependent.hpp"
#include <algorithm>
#include <array>
#include <cstring>

namespace cs2::backends::hyperv {
void ReadChannel::read_dependent(std::span<memory::DependentRead> requests) noexcept {
    using namespace native::abi;
    const auto valid = memory::valid_dependent_reads(requests);
    for (auto& r : requests) {
        r.pointer = 0;
        r.result = {valid ? memory::TransferStatus::Unavailable : memory::TransferStatus::InvalidRange};
    }
    if (!valid)
        return;
    std::array<DependentItem, dependent_max_items> items{};
    std::array<std::size_t, dependent_max_items> indices{};
    std::size_t count = 0, bytes = 0;
    auto flush = [&] {
        if (!count)
            return;
        DependentResult result{};
        const auto published =
            native::read_dependent(binding_->address_space.cr3, std::span(items).first(count), result, &counters_);
        for (std::size_t i = 0; i < count; ++i) {
            auto& r = requests[indices[i]];
            if (!published) {
                r.result = {memory::TransferStatus::BackendFailure};
                continue;
            }
            switch (result.status[i]) {
            case DependentStatus::Success:
                r.result = {memory::TransferStatus::Complete, r.bytes};
                r.pointer = result.pointers[i];
                break;
            case DependentStatus::InvalidAddress:
                r.result = {memory::TransferStatus::InvalidRange};
                break;
            case DependentStatus::DependencyFailed:
                r.result = {memory::TransferStatus::DependencyFailed};
                break;
            default:
                r.result = {memory::TransferStatus::Unavailable};
                break;
            }
        }
        count = bytes = 0;
    };
    // 跨批只传递前一批已确认成功的指针观察，不重新读取父项，也不截断计划。
    const auto resolved = [&](memory::DependentRead& r) -> std::optional<memory::Address> {
        if (r.parent == memory::direct_read)
            return r.address;
        const auto& parent = requests[r.parent];
        if (!parent.result.complete(sizeof(memory::Address))) {
            r.result = {memory::TransferStatus::DependencyFailed};
            return {};
        }
        const auto address = memory::relative_address(parent.pointer, r.address, r.bytes);
        if (!address)
            r.result = {memory::TransferStatus::InvalidRange};
        return address;
    };
    for (std::size_t i = 0; i < requests.size(); ++i) {
        auto& r = requests[i];
        if (!r.bytes || r.bytes > dependent_max_bytes) {
            flush();
            if (const auto address = resolved(r))
                r.result = r.bytes ? read(*address, r.destination, r.bytes)
                                   : memory::TransferResult{memory::TransferStatus::Complete};
            continue; // 超大连续复制保留现有单范围接口，不切碎、不截断。
        }
        if (count == items.size() || r.bytes > dependent_max_bytes - bytes) {
            if (count == items.size())
                ++counters_.item_splits;
            else
                ++counters_.byte_splits;
            flush();
        }
        auto address = r.address;
        auto parent = dependent_direct;
        if (r.parent != memory::direct_read) {
            const auto found = std::find(indices.begin(), indices.begin() + count, r.parent);
            if (found != indices.begin() + count)
                parent = static_cast<std::uint16_t>(found - indices.begin());
            else {
                const auto observed = resolved(r);
                if (!observed)
                    continue;
                address = *observed;
            }
        }
        items[count] = {address,
                        reinterpret_cast<std::uint64_t>(r.destination),
                        static_cast<std::uint32_t>(r.bytes),
                        parent,
                        r.kind == memory::ReadKind::Pointer ? DependentKind::Pointer : DependentKind::Copy,
                        0};
        indices[count++] = i;
        bytes += r.bytes;
    }
    flush();
    for (auto& r : requests)
        if (r.kind == memory::ReadKind::Copy && r.bytes && !r.result.complete(r.bytes))
            std::memset(r.destination, 0, r.bytes);
}
} // namespace cs2::backends::hyperv
