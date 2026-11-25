#include "runtime/binding.hpp"

namespace cs2::runtime {
Binding::Binding(memory::IBackend& backend, memory::IProcessCatalog& catalog, const Target& target)
    : backend_(backend), catalog_(catalog), target_(target) {}
memory::ISession* Binding::acquire() {
    if (session_ && !refresh_pending_)
        return session_.get();
    const bool refreshing = session_ != nullptr;
    std::optional<memory::ProcessIdentity> process;
    if (refreshing) {
        process = session_->identity();
    } else {
        try {
            process = catalog_.find(target_.name, target_.pid);
        } catch (const memory::ReadUnavailable& error) {
            throw BindingPending(BindingPending::Stage::Discovery, error.what());
        }
    }
    if (!process)
        return nullptr;
    try {
        auto replacement = refreshing ? session_->rebind() : backend_.attach(*process, target_.access);
        if (!replacement)
            throw std::logic_error("Backend did not provide a session");
        if (replacement->identity() != *process)
            throw memory::ReadUnavailable("Backend returned a different process instance during binding");
        session_ = std::move(replacement);
        refresh_pending_ = false;
        unreadable_checks_ = 0;
    } catch (const std::runtime_error& error) {
        if (refreshing) {
            // 确认过的地址空间变化可以重试 attach，但不能因临时失败丢失旧身份或换绑其他实例。
            const auto state = discard_if_gone();
            if (target_.pid && (state == memory::ProcessState::Exited || state == memory::ProcessState::Changed))
                throw BindingPending(BindingPending::Stage::SelectedGone, error.what());
            throw BindingPending(BindingPending::Stage::Attach, error.what());
        }
        // 绑定失败本身不刷新发现结果；仅已确认的 PID 消失或创建时间变化才使目录失效。
        try {
            const auto identity = backend_.find_process_identity(process->pid);
            if (identity != process)
                catalog_.invalidate();
        } catch (const std::runtime_error&) {
        }
        throw BindingPending(BindingPending::Stage::Attach, error.what());
    }
    return session_.get();
}
memory::ProcessState Binding::observe() {
    using State = memory::ProcessState;
    if (!session_)
        throw std::logic_error("Cannot observe an unbound process");
    auto state = State::Unreadable;
    try {
        state = session_->state();
    } catch (const memory::ReadUnavailable&) {
        // 与后端返回 Unreadable 使用同一条恢复路径。
    }
    if (state != State::Unreadable) {
        unreadable_checks_ = 0;
        return state;
    }
    constexpr unsigned recheck_after = 3;
    if (++unreadable_checks_ < recheck_after)
        return state;
    unreadable_checks_ = 0;
    // 旧对象可能已经被回收。用活动链表独立确认同一个 PID 的创建时间，保留仍存活的实例。
    try {
        const auto expected = session_->identity();
        const auto current = backend_.find_process_identity(expected.pid);
        if (!current)
            return State::Exited;
        if (*current != expected)
            return State::Changed;
    } catch (const std::runtime_error&) {
        // 链表暂时不可读，等待下一次核对。
    }
    return state;
}
memory::ProcessState Binding::discard_if_gone() {
    const auto state = observe();
    if (state == memory::ProcessState::Exited || state == memory::ProcessState::Changed)
        release();
    else if (state == memory::ProcessState::AddressSpaceChanged)
        refresh_address_space();
    return state;
}
void Binding::refresh_address_space() {
    if (!session_)
        throw std::logic_error("Cannot refresh an unbound process");
    refresh_pending_ = true;
}
void Binding::release() {
    session_.reset();
    refresh_pending_ = false;
    unreadable_checks_ = 0;
    catalog_.invalidate();
}
} // namespace cs2::runtime
