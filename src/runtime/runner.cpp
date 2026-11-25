#include "diagnostics/events.hpp"
#include "runtime/run_state.hpp"
#include <ostream>
#include <system_error>

namespace cs2::runtime {
bool RunState::running() const {
    return !environment.control.stopping() && (!limits.samples || sequence < limits.samples) &&
           (!limits.duration_seconds ||
            std::chrono::duration<double>(Steady::now() - started).count() < *limits.duration_seconds);
}
void RunState::status(model::Availability state, model::ReasonCode reason, std::optional<std::uint64_t> value) {
    using model::Availability;
    if (environment.output && state != Availability::Ready)
        environment.output->unavailable(state, reason, value);
    const model::SourceStatus current{state, value, {}, reason};
    if (last_status == current)
        return;
    last_status = current;
    const auto level = state == Availability::Unavailable ? diagnostics::Level::Warning : diagnostics::Level::Info;
    diagnostics::event(log, diagnostics::EventId::StatusChanged, level)
        << " availability=" << static_cast<unsigned>(state);
    if (reason != model::ReasonCode::None)
        log << " reason_code=" << static_cast<std::uint32_t>(reason);
    if (value)
        log << " epoch=" << *value;
    log << '\n';
}

void RunState::wait_for_source() {
    const auto now = Steady::now();
    if (now >= next_status) {
        if (environment.output)
            environment.output->report(log);
        log.flush();
        next_status = now + std::chrono::seconds(3);
    }
    environment.control.wait_until(now + std::chrono::milliseconds(500));
}

void RunState::retry(model::ReasonCode reason, std::optional<std::uint64_t> value) {
    status(model::Availability::Unavailable, reason, value);
    wait_for_source();
}

int run(const Target& target, const Limits& limits, const Environment& environment, std::ostream& log) {
    using model::Availability;
    using model::ReasonCode;
    auto& backend = environment.backend;
    auto& catalog = environment.catalog;
    RunState progress{limits, environment, log};
    const auto selected_gone = [&](memory::ProcessState state) {
        return target.pid && progress.running() && retired(state);
    };
    const auto selected_exit = [&] {
        progress.status(Availability::Unavailable, ReasonCode::SourceRetired,
                        progress.epoch ? std::optional(progress.epoch) : std::nullopt);
        return 3;
    };
    Binding binding(backend, catalog, target);
    while (progress.running()) {
        memory::ISession* session = nullptr;
        try {
            session = binding.acquire();
        } catch (const BindingPending& error) {
            if (!progress.running())
                break;
            if (error.stage == BindingPending::Stage::SelectedGone)
                return selected_exit();
            progress.retry(error.stage == BindingPending::Stage::Discovery ? ReasonCode::DiscoveryUnavailable
                                                                           : ReasonCode::BindingUnavailable);
            continue;
        }
        if (!progress.running())
            break;
        if (!session) {
            progress.status(Availability::Waiting);
            const auto now = Steady::now();
            if (limits.wait_seconds &&
                std::chrono::duration<double>(now - progress.started).count() >= *limits.wait_seconds) {
                diagnostics::report_fault(log, {ReasonCode::WaitExpired});
                return 3;
            }
            progress.wait_for_source();
            continue;
        }
        auto state = memory::ProcessState::Current;
        {
            std::unique_ptr<capture::ISource> source;
            try {
                source = environment.factory.open(*session, environment.clock);
                if (!source)
                    throw std::logic_error("Capture factory did not provide a source");
            } catch (const capture::IncompatibleSource&) {
                if (!progress.running())
                    break;
                progress.status(Availability::Unavailable, ReasonCode::SourceIncompatible,
                                progress.epoch ? std::optional(progress.epoch) : std::nullopt);
                log.flush();
                // 相同实例与配置保持不可用。只观察实例生命周期，避免反复打开采集器。
                while (progress.running()) {
                    progress.wait_for_source();
                    if (!progress.running())
                        break;
                    const auto observed = binding.observe();
                    if (retired(observed)) {
                        if (selected_gone(observed))
                            return selected_exit();
                        binding.release();
                        break;
                    }
                    if (observed == memory::ProcessState::AddressSpaceChanged) {
                        binding.refresh_address_space();
                        break;
                    }
                }
                continue;
            } catch (const memory::ReadUnavailable&) {
                if (selected_gone(binding.discard_if_gone()))
                    return selected_exit();
                progress.retry(ReasonCode::ContextPending);
                continue;
            } catch (const std::system_error& error) {
                if (error.code() != std::errc::resource_unavailable_try_again &&
                    error.code() != std::errc::not_enough_memory)
                    throw;
                progress.retry(ReasonCode::WorkersUnavailable);
                continue;
            } catch (const std::runtime_error&) {
                const auto binding_state = binding.discard_if_gone();
                if (binding_state == memory::ProcessState::Current)
                    throw;
                if (selected_gone(binding_state))
                    return selected_exit();
                progress.retry(ReasonCode::ContextPending);
                continue;
            }
            ++progress.epoch;
            diagnostics::event(log, diagnostics::EventId::SourceOpened)
                << " epoch=" << progress.epoch << " build=" << source->build() << " binding=" << session->binding_id()
                << '\n';
            log.flush();
            state = progress.capture(binding, *source);
        } // 完整销毁采集器及其 worker，再处理绑定的释放或替换。
        if (progress.running()) {
            if (state == memory::ProcessState::AddressSpaceChanged)
                binding.refresh_address_space();
            else
                binding.release();
        }
        if (selected_gone(state))
            return selected_exit();
    }
    progress.status(Availability::Stopping, ReasonCode::None,
                    progress.epoch ? std::optional(progress.epoch) : std::nullopt);
    diagnostics::event(log, diagnostics::EventId::Stopped)
        << " captures=" << progress.sequence << " failed=" << progress.failures
        << " missed_opportunities=" << progress.missed;
    if (const auto* output = environment.output)
        if (const auto count = output->coalesced())
            log << " output_coalesced=" << *count;
    log << '\n';
    return 0;
}
} // namespace cs2::runtime
