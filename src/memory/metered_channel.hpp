#pragma once
#include "memory/read_channel.hpp"

namespace cs2::memory {
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
