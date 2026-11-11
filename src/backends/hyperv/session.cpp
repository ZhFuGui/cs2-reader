#include "backends/hyperv/session.hpp"
#include "backends/hyperv/channel.hpp"
namespace cs2::backends::hyperv {
memory::ProcessState Session::state() const {
    return native::observe_process(binding_->layout, binding_->process, binding_->address_space,
                                    binding_->caller_cr3);
}
std::unique_ptr<memory::IReadChannel> Session::open_reader() const {
    return std::make_unique<ReadChannel>(binding_);
}
std::unique_ptr<memory::IWriteChannel> Session::open_writer() const {
    if (binding_->access != memory::AccessMode::ReadWrite)
        return {};
    return std::make_unique<WriteChannel>(binding_);
}
} // namespace cs2::backends::hyperv
