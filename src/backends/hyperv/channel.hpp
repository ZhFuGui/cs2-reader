#pragma once
#include "backends/hyperv/native/hypercall.hpp"
#include "backends/hyperv/session.hpp"
namespace cs2::backends::hyperv {
class ReadChannel final : public memory::IReadChannel {
  public:
    explicit ReadChannel(std::shared_ptr<const Binding> binding) : binding_(std::move(binding)) {}
    memory::TransferResult read(memory::Address, void*, std::size_t) noexcept override;
    void read_many(std::span<memory::ReadRequest>) noexcept override;
    memory::ReadMetrics take_backend_metrics() noexcept override;

  private:
    const std::shared_ptr<const Binding> binding_;
    native::ReadCounters counters_;
};
class WriteChannel final : public memory::IWriteChannel {
  public:
    explicit WriteChannel(std::shared_ptr<const Binding> binding) : binding_(std::move(binding)) {}
    memory::TransferResult write(memory::Address, const void*, std::size_t) noexcept override;

  private:
    const std::shared_ptr<const Binding> binding_;
};
} // namespace cs2::backends::hyperv
