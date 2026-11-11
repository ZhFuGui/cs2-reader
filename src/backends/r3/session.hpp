#pragma once
#include "memory/session.hpp"
#include "platform/windows/unique_handle.hpp"
#include <memory>

namespace cs2::backends::r3 {
struct Binding {
    platform::windows::UniqueHandle handle;
    memory::ProcessIdentity process;
    memory::Address peb = 0;
    memory::AccessMode access = memory::AccessMode::ReadOnly;
};
class Session final : public memory::ISession {
  public:
    explicit Session(std::shared_ptr<const Binding> binding) : binding_(std::move(binding)) {}
    memory::ProcessIdentity identity() const noexcept override { return binding_->process; }
    memory::Address peb() const noexcept override { return binding_->peb; }
    bool alive() const override;
    std::unique_ptr<memory::IReadChannel> open_reader() const override;
    std::unique_ptr<memory::IWriteChannel> open_writer() const override;

  private:
    std::shared_ptr<const Binding> binding_;
};
} // namespace cs2::backends::r3
