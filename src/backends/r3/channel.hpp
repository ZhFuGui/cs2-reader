#pragma once
#include "backends/r3/session.hpp"
namespace cs2::backends::r3 {
class ReadChannel final : public memory::IReadChannel {
  public:
    explicit ReadChannel(std::shared_ptr<const Binding> binding) : binding_(std::move(binding)) {}
    memory::TransferResult read(memory::Address, void*, std::size_t) noexcept override;

  private:
    const std::shared_ptr<const Binding> binding_;
};
class WriteChannel final : public memory::IWriteChannel {
  public:
    explicit WriteChannel(std::shared_ptr<const Binding> binding) : binding_(std::move(binding)) {}
    memory::TransferResult write(memory::Address, const void*, std::size_t) noexcept override;

  private:
    const std::shared_ptr<const Binding> binding_;
};
} // namespace cs2::backends::r3
