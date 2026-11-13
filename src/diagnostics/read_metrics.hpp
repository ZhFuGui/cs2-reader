#pragma once
#include <cstdint>

namespace cs2::diagnostics {
struct ReadMetrics {
    std::uint64_t calls = 0, requested = 0, received = 0, failures = 0;
    std::uint64_t hypercall_single = 0, hypercall_batch = 0, batch_items = 0, batch_errors = 0;
    std::uint64_t batch_item_splits = 0, batch_byte_splits = 0;
    // dependent 是 batch 的子集；现有协议中的 batch 汇总仍包含全部批读。
    std::uint64_t hypercall_dependent = 0, dependent_items = 0;
    ReadMetrics& operator+=(const ReadMetrics& rhs) noexcept {
        calls += rhs.calls;
        requested += rhs.requested;
        received += rhs.received;
        failures += rhs.failures;
        hypercall_single += rhs.hypercall_single;
        hypercall_batch += rhs.hypercall_batch;
        batch_items += rhs.batch_items;
        batch_errors += rhs.batch_errors;
        batch_item_splits += rhs.batch_item_splits;
        batch_byte_splits += rhs.batch_byte_splits;
        hypercall_dependent += rhs.hypercall_dependent;
        dependent_items += rhs.dependent_items;
        return *this;
    }
};
} // namespace cs2::diagnostics
