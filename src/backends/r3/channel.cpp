#include "backends/r3/channel.hpp"
namespace cs2::backends::r3 {
namespace {
memory::TransferResult result(NTSTATUS status, std::size_t received, std::size_t requested) noexcept {
    using memory::TransferStatus;
    if (received > requested)
        return {TransferStatus::Unavailable, 0, static_cast<std::uint32_t>(status)};
    const auto code = platform::windows::nt_success(status) && received == requested ? TransferStatus::Complete
                      : status == platform::windows::status_access_denied            ? TransferStatus::AccessDenied
                      : received                                                     ? TransferStatus::Partial
                                                                                     : TransferStatus::Unavailable;
    return {code, received, static_cast<std::uint32_t>(status)};
}
} // namespace
memory::TransferResult ReadChannel::read(memory::Address address, void* destination, std::size_t bytes) noexcept {
    if ((!destination && bytes) || !memory::user_range(address, bytes))
        return {memory::TransferStatus::InvalidRange};
    if (!bytes)
        return {memory::TransferStatus::Complete};
    SIZE_T received = 0;
    const auto status =
        ::NtReadVirtualMemory(binding_->handle.get(), reinterpret_cast<void*>(address), destination, bytes, &received);
    return result(status, received, bytes);
}
memory::TransferResult WriteChannel::write(memory::Address address, const void* source, std::size_t bytes) noexcept {
    if (binding_->access != memory::AccessMode::ReadWrite)
        return {memory::TransferStatus::AccessDenied};
    if ((!source && bytes) || !memory::user_range(address, bytes))
        return {memory::TransferStatus::InvalidRange};
    if (!bytes)
        return {memory::TransferStatus::Complete};
    SIZE_T transferred = 0;
    const auto status = ::NtWriteVirtualMemory(binding_->handle.get(), reinterpret_cast<void*>(address),
                                               const_cast<void*>(source), bytes, &transferred);
    return result(status, transferred, bytes);
}
} // namespace cs2::backends::r3
