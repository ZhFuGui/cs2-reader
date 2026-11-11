#pragma once
#include "backends/hyperv/native/kernel_binding.hpp"
#include "memory/session.hpp"
namespace cs2::backends::hyperv {
struct Binding {
    memory::ProcessIdentity process;
    native::ProcessBinding address_space;
    std::uint64_t caller_cr3 = 0;
    memory::AccessMode access = memory::AccessMode::ReadOnly;
};
class Session final : public memory::ISession {
  public:
    explicit Session(std::shared_ptr<const Binding> binding) : binding_(std::move(binding)) {}
    memory::ProcessIdentity identity() const noexcept override { return binding_->process; }
    memory::Address peb() const noexcept override { return binding_->address_space.peb; }
    bool alive() const override;
    std::unique_ptr<memory::IReadChannel> open_reader() const override;
    std::unique_ptr<memory::IWriteChannel> open_writer() const override;

  private:
    const std::shared_ptr<const Binding> binding_;
};
} // namespace cs2::backends::hyperv
