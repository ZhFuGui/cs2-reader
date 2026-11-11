#include "backends/hyperv/backend.hpp"
#include "backends/hyperv/native/batch.hpp"
#include "backends/hyperv/native/hypercall.hpp"
#include "backends/hyperv/session.hpp"
#include <stdexcept>
namespace cs2::backends {
namespace {
class HyperVBackend final : public memory::IBackend {
  public:
    std::unique_ptr<memory::ISession> attach(memory::ProcessIdentity process, memory::AccessMode access) override {
        initialize();
        const auto address_space = hyperv::native::resolve_process(*layout_, process, caller_cr3_);
        auto binding = std::make_shared<hyperv::Binding>();
        binding->process = process;
        binding->address_space = address_space;
        binding->layout = *layout_;
        binding->caller_cr3 = caller_cr3_;
        binding->access = access;
        auto session = std::make_unique<hyperv::Session>(std::move(binding));
        if (session->state() != memory::ProcessState::Current)
            throw std::runtime_error("Process identity changed while attaching the hypervisor backend");
        return session;
    }
    std::optional<memory::ProcessIdentity> find_process_hint(std::string_view image_name) override {
        initialize();
        return hyperv::native::discover_process(*layout_, image_name, caller_cr3_);
    }
  private:
    void initialize() {
        if (layout_) return;
        const auto caller_cr3 = hyperv::native::probe_address_space();
        if (!caller_cr3)
            throw std::runtime_error("hypervisor handshake or self-read failed; no fallback path");
        if (!hyperv::native::probe_batch(caller_cr3))
            throw std::runtime_error("hypervisor batch self-read failed; batch-capable backend required");
        if (!hyperv::native::probe_list_find(caller_cr3))
            throw std::runtime_error("hypervisor list-find self-read failed; operation 12 required");
        layout_ = hyperv::native::resolve_kernel_layout(caller_cr3);
        caller_cr3_ = caller_cr3;
    }
    std::uint64_t caller_cr3_ = 0;
    std::optional<hyperv::native::KernelLayout> layout_;
};
} // namespace
std::unique_ptr<memory::IBackend> make_hyperv_backend() {
    return std::make_unique<HyperVBackend>();
}
} // namespace cs2::backends
