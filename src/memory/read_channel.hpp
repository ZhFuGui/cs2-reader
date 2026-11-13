#pragma once
#include "diagnostics/read_metrics.hpp"
#include "memory/dependent_read.hpp"
#include "memory/transfer_result.hpp"
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>

namespace cs2::memory {
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
    // 默认实现供离线通道复用，不引入后台队列。
    virtual void read_many(std::span<ReadRequest> requests) noexcept {
        for (auto& request : requests)
            request.result = read(request.address, request.destination, request.bytes);
    }
    // 默认解释供离线通道验证；生产后端覆盖为原生依赖批读。不是原子快照。
    virtual void read_dependent(std::span<DependentRead>) noexcept;
    virtual diagnostics::ReadMetrics take_backend_metrics() noexcept { return {}; }
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
    void read_dependent(std::span<DependentRead> requests) noexcept override {
        source_.read_dependent(requests);
        for (const auto& request : requests)
            account(request.result, request.bytes);
    }
    [[nodiscard]] diagnostics::ReadMetrics take_metrics() noexcept {
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
    diagnostics::ReadMetrics metrics_;
};
} // namespace cs2::memory
