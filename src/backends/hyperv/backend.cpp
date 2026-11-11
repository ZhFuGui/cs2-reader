#include "backends/hyperv/backend.hpp"
#include "backends/hyperv/native/batch.hpp"
#include "backends/hyperv/native/hypercall.hpp"
#include "backends/hyperv/session.hpp"
#include "platform/windows/process_catalog.hpp"
#include <stdexcept>
namespace cs2::backends {
namespace {
class HyperVBackend final : public memory::IBackend {
  public:
    memory::BackendKind kind() const noexcept override { return memory::BackendKind::HyperV; }
    std::unique_ptr<memory::ISession> attach(memory::ProcessIdentity process, memory::AccessMode access) override {
        const auto caller_cr3 = hyperv::native::probe_address_space();
        if (!caller_cr3)
            throw std::runtime_error("hypervisor handshake or self-read failed; no fallback path");
        if (!hyperv::native::probe_batch(caller_cr3))
            throw std::runtime_error("hypervisor batch self-read failed; batch-capable backend required");
        const auto layout = hyperv::native::resolve_kernel_layout(caller_cr3);
        const auto address_space = hyperv::native::resolve_process(layout, process, caller_cr3);
        const auto observed = platform::windows::query_identity(process.pid);
        if (!observed || *observed != process)
            throw std::runtime_error("Process identity changed while attaching the hypervisor backend");
        auto binding = std::make_shared<hyperv::Binding>();
        binding->process = process;
        binding->address_space = address_space;
        binding->caller_cr3 = caller_cr3;
        binding->access = access;
        return std::make_unique<hyperv::Session>(std::move(binding));
    }
};
} // namespace
std::unique_ptr<memory::IBackend> make_hyperv_backend() {
    return std::make_unique<HyperVBackend>();
}
} // namespace cs2::backends
