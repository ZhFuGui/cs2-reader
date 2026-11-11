#include "backends/r3/session.hpp"
#include "backends/r3/channel.hpp"
namespace cs2::backends::r3 {
bool Session::alive() const {
    platform::windows::BasicProcessInformation basic{};
    const auto status =
        ::NtQueryInformationProcess(binding_->handle.get(), ProcessBasicInformation, &basic, sizeof(basic), nullptr);
    return platform::windows::nt_success(status) && basic.pid == binding_->process.pid &&
           basic.exit_status == static_cast<NTSTATUS>(0x103);
}
std::unique_ptr<memory::IReadChannel> Session::open_reader() const {
    return std::make_unique<ReadChannel>(binding_);
}
std::unique_ptr<memory::IWriteChannel> Session::open_writer() const {
    if (binding_->access != memory::AccessMode::ReadWrite)
        return {};
    return std::make_unique<WriteChannel>(binding_);
}
} // namespace cs2::backends::r3
