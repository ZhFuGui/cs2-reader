#include "backends/hyperv/session.hpp"
#include "backends/hyperv/channel.hpp"
#include "platform/windows/process_catalog.hpp"
namespace cs2::backends::hyperv {
bool Session::alive() const {
    const auto identity = platform::windows::query_identity(binding_->process.pid);
    return identity && *identity == binding_->process;
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
