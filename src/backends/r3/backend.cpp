#include "backends/r3/backend.hpp"
#include "backends/r3/session.hpp"
#include <stdexcept>

namespace cs2::backends {
namespace {
class R3Backend final : public memory::IBackend {
  public:
    memory::BackendKind kind() const noexcept override { return memory::BackendKind::R3; }
    std::unique_ptr<memory::ISession> attach(memory::ProcessIdentity process, memory::AccessMode access) override {
        using namespace platform::windows;
        auto binding = std::make_shared<r3::Binding>();
        NativeClientId client{reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(process.pid)), nullptr};
        OBJECT_ATTRIBUTES attributes{};
        attributes.Length = sizeof(attributes);
        const ACCESS_MASK rights =
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ |
            (access == memory::AccessMode::ReadWrite ? PROCESS_VM_WRITE | PROCESS_VM_OPERATION : 0);
        HANDLE handle = nullptr;
        auto status = ::NtOpenProcess(&handle, rights, &attributes, &client);
        if (!nt_success(status))
            throw std::runtime_error("NtOpenProcess failed: " + std::to_string(static_cast<std::uint32_t>(status)));
        binding->handle = UniqueHandle(handle);
        ProcessTimes times{};
        status = ::NtQueryInformationProcess(handle, static_cast<PROCESSINFOCLASS>(4), &times, sizeof(times), nullptr);
        if (!nt_success(status) || static_cast<std::uint64_t>(times.created.QuadPart) != process.creation_time)
            throw std::runtime_error("Process instance changed while opening the native handle");
        ULONG_PTR wow64 = 0;
        status = ::NtQueryInformationProcess(handle, static_cast<PROCESSINFOCLASS>(26), &wow64, sizeof(wow64), nullptr);
        if (!nt_success(status) || wow64)
            throw std::runtime_error("Only native x64 target processes are supported");
        BasicProcessInformation basic{};
        status = ::NtQueryInformationProcess(handle, ProcessBasicInformation, &basic, sizeof(basic), nullptr);
        if (!nt_success(status) || basic.pid != process.pid ||
            !memory::user_range(reinterpret_cast<memory::Address>(basic.peb)))
            throw std::runtime_error("NtQueryInformationProcess returned an invalid x64 process binding");
        binding->process = process;
        binding->peb = reinterpret_cast<memory::Address>(basic.peb);
        binding->access = access;
        return std::make_unique<r3::Session>(std::move(binding));
    }
};
} // namespace
std::unique_ptr<memory::IBackend> make_r3_backend() {
    return std::make_unique<R3Backend>();
}
} // namespace cs2::backends
