#include "runtime/binding.hpp"

namespace cs2::runtime {
Binding::Binding(memory::IBackend& backend, memory::IProcessCatalog& catalog, const Target& target)
    : backend_(backend), catalog_(catalog), target_(target) {}
memory::ISession* Binding::acquire() {
    if (session_)
        return session_.get();
    std::optional<memory::ProcessIdentity> process;
    try {
        process = catalog_.find(target_.name, target_.pid);
    } catch (const memory::ReadUnavailable& error) {
        throw BindingPending(BindingPending::Stage::Discovery, error.what());
    }
    if (!process) {
        try {
            const auto hint = backend_.find_process_hint(target_.image_name);
            if (hint && (!target_.pid || hint->pid == *target_.pid)) {
                catalog_.invalidate();
                process = catalog_.find(target_.name, target_.pid);
            }
        } catch (const std::runtime_error& error) {
            throw BindingPending(BindingPending::Stage::Discovery, error.what());
        }
    }
    if (!process)
        return nullptr;
    try {
        session_ = backend_.attach(*process, target_.access);
    } catch (const std::runtime_error& error) {
        // 绑定失败本身不触发原生枚举；仅后端明确确认身份变化才使快照失效。
        try {
            const auto hint = backend_.find_process_hint(target_.image_name);
            if (!hint || *hint != *process)
                catalog_.invalidate();
        } catch (const std::runtime_error&) {
        }
        throw BindingPending(BindingPending::Stage::Attach, error.what());
    }
    if (!session_)
        throw std::logic_error("Backend did not provide a session");
    return session_.get();
}
memory::ProcessState Binding::observe() const {
    if (!session_)
        throw std::logic_error("Cannot observe an unbound process");
    try {
        return session_->state();
    } catch (const memory::ReadUnavailable&) {
        return memory::ProcessState::Unreadable;
    }
}
memory::ProcessState Binding::discard_if_gone() {
    const auto state = observe();
    if (state == memory::ProcessState::Exited || state == memory::ProcessState::Changed)
        release();
    return state;
}
void Binding::release() {
    session_.reset();
    catalog_.invalidate();
}
} // namespace cs2::runtime
