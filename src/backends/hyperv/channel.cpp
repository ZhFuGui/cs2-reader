#include "backends/hyperv/channel.hpp"
#include "backends/hyperv/native/batch.hpp"
#include "backends/hyperv/native/hypercall.hpp"
#include <array>
#include <utility>
namespace cs2::backends::hyperv {
memory::TransferResult ReadChannel::read(memory::Address address, void* output, std::size_t bytes) noexcept {
    if ((!output && bytes) || !memory::user_range(address, bytes))
        return {memory::TransferStatus::InvalidRange};
    if (!bytes)
        return {memory::TransferStatus::Complete};
    const auto transferred =
        native::read(binding_->caller_cr3, binding_->address_space.cr3, address, output, bytes, &counters_);
    return {transferred == bytes ? memory::TransferStatus::Complete : memory::TransferStatus::Unavailable, transferred};
}
void ReadChannel::read_many(std::span<memory::ReadRequest> requests) noexcept {
    std::array<native::abi::BatchItem, native::abi::batch_max_items> items{};
    std::array<std::size_t, native::abi::batch_max_items> indices{};
    std::size_t count = 0, bytes = 0;
    auto flush = [&] {
        if (!count)
            return;
        if (count == 1) {
            auto& r = requests[indices[0]];
            r.result = read(r.address, r.destination, r.bytes);
        } else {
            alignas(8) std::uint64_t mask = 0;
            const bool published =
                native::read_batch(binding_->address_space.cr3, std::span(items).first(count), mask, &counters_);
            for (std::size_t i = 0; i < count; ++i) {
                auto& r = requests[indices[i]];
                const bool complete = published && (mask & (1ULL << i));
                r.result = {complete    ? memory::TransferStatus::Complete
                            : published ? memory::TransferStatus::Unavailable
                                        : memory::TransferStatus::BackendFailure,
                            complete ? r.bytes : 0};
            }
        }
        count = bytes = 0;
    };
    for (std::size_t i = 0; i < requests.size(); ++i) {
        auto& r = requests[i];
        r.result = {memory::TransferStatus::InvalidRange};
        if ((!r.destination && r.bytes) || !memory::user_range(r.address, r.bytes) ||
            (r.bytes && !memory::user_range(reinterpret_cast<std::uint64_t>(r.destination), r.bytes)))
            continue;
        if (!r.bytes) {
            r.result = {memory::TransferStatus::Complete};
            continue;
        }
        if (r.bytes > native::abi::batch_max_bytes) {
            flush(); // 长连续范围保留一次单范围复制，不因批量上限额外切碎。
            r.result = read(r.address, r.destination, r.bytes);
            continue;
        }
        if (count == items.size() || r.bytes > native::abi::batch_max_bytes - bytes) {
            if (count == items.size())
                ++counters_.item_splits;
            else
                ++counters_.byte_splits;
            flush();
        }
        items[count] = {r.address, reinterpret_cast<std::uint64_t>(r.destination), r.bytes};
        indices[count++] = i;
        bytes += r.bytes;
    }
    flush();
}
memory::ReadMetrics ReadChannel::take_backend_metrics() noexcept {
    const auto counters = std::exchange(counters_, {});
    memory::ReadMetrics result;
    result.hypercall_single = counters.single;
    result.hypercall_batch = counters.batch;
    result.batch_items = counters.items;
    result.batch_errors = counters.errors;
    result.batch_item_splits = counters.item_splits;
    result.batch_byte_splits = counters.byte_splits;
    return result;
}
memory::TransferResult WriteChannel::write(memory::Address address, const void* input, std::size_t bytes) noexcept {
    if (binding_->access != memory::AccessMode::ReadWrite)
        return {memory::TransferStatus::AccessDenied};
    if ((!input && bytes) || !memory::user_range(address, bytes))
        return {memory::TransferStatus::InvalidRange};
    if (!bytes)
        return {memory::TransferStatus::Complete};
    const auto transferred = native::write(binding_->caller_cr3, binding_->address_space.cr3, address, input, bytes);
    return {transferred == bytes ? memory::TransferStatus::Complete : memory::TransferStatus::Unavailable, transferred};
}
} // namespace cs2::backends::hyperv
