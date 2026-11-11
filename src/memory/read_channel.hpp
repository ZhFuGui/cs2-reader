#pragma once
#include "memory/transfer_result.hpp"
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>

namespace cs2::memory {
struct ReadMetrics {
    std::uint64_t calls = 0, requested = 0, received = 0, failures = 0;
    std::uint64_t hypercall_single = 0, hypercall_batch = 0, batch_items = 0, batch_errors = 0;
    std::uint64_t batch_item_splits = 0, batch_byte_splits = 0;
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
        return *this;
    }
};
struct ReadRequest {
    Address address;
    void* destination;
    std::size_t bytes;
    TransferResult result{};
};
class IReadChannel {
  public:
    virtual ~IReadChannel() = default;
    virtual TransferResult read(Address address, void* destination, std::size_t bytes) noexcept = 0;
    // 同步完成；调用期间请求及互不重叠的目标缓冲必须独占、稳定。
    // 默认实现供 R3 和离线通道复用，不引入后台队列。
    virtual void read_many(std::span<ReadRequest> requests) noexcept {
        for (auto& request : requests)
            request.result = read(request.address, request.destination, request.bytes);
    }
    virtual ReadMetrics take_backend_metrics() noexcept { return {}; }
};
template <class T> [[nodiscard]] std::optional<T> read_value(IReadChannel& channel, Address address) noexcept {
    static_assert(std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>);
    T scratch{};
    if (!channel.read(address, &scratch, sizeof(T)).complete(sizeof(T)))
        return std::nullopt;
    return scratch;
}
class MeteredChannel final : public IReadChannel {
  public:
    explicit MeteredChannel(IReadChannel& source) : source_(source) {}
    TransferResult read(Address address, void* destination, std::size_t bytes) noexcept override {
        auto result = source_.read(address, destination, bytes);
        account(result, bytes);
        return result;
    }
    void read_many(std::span<ReadRequest> requests) noexcept override {
        source_.read_many(requests);
        for (const auto& request : requests)
            account(request.result, request.bytes);
    }
    [[nodiscard]] ReadMetrics take_metrics() noexcept {
        auto result = metrics_;
        result += source_.take_backend_metrics();
        metrics_ = {};
        return result;
    }

  private:
    void account(const TransferResult& result, std::size_t bytes) noexcept {
        ++metrics_.calls;
        metrics_.requested += bytes;
        metrics_.received += result.transferred;
        if (!result.complete(bytes))
            ++metrics_.failures;
    }
    IReadChannel& source_;
    ReadMetrics metrics_;
};
} // namespace cs2::memory
