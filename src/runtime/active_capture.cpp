#include "capture/cadence.hpp"
#include "diagnostics/capture_report.hpp"
#include "runtime/run_state.hpp"
#include <ostream>

namespace cs2::runtime {
namespace {
model::ReasonCode binding_reason(memory::ProcessState state) {
    if (retired(state))
        return model::ReasonCode::SourceRetired;
    if (state == memory::ProcessState::AddressSpaceChanged)
        return model::ReasonCode::SourceContextChanged;
    return model::ReasonCode::BindingUnavailable;
}
} // namespace

memory::ProcessState RunState::capture(Binding& binding, capture::ISource& source) {
    using model::Availability;
    using model::ReasonCode;
    auto& frequency = environment.frequency;
    auto& control = environment.control;
    auto* output = environment.output;
    capture::Cadence cadence(frequency.current(), Steady::now());
    auto last_alive = Steady::now();
    next_status = last_alive;
    auto state = memory::ProcessState::Current;
    const auto check_binding = [&] {
        state = binding.observe();
        if (state == memory::ProcessState::Current)
            last_alive = Steady::now();
    };
    while (running()) {
        const auto now = Steady::now();
        cadence.configure(frequency.current(), now);
        if (state == memory::ProcessState::Unreadable || now - last_alive >= std::chrono::seconds(1)) {
            check_binding();
            if (state == memory::ProcessState::Unreadable) {
                retry(ReasonCode::BindingUnavailable, epoch);
                continue;
            }
            if (state != memory::ProcessState::Current) {
                status(Availability::Unavailable, binding_reason(state), epoch);
                break;
            }
        }
        try {
            auto snapshot = source.capture(sequence + 1, epoch);
            snapshot.capture_target_hz = cadence.rate().hz;
            ++sequence;
            if (now >= next_status || limits.samples == 1) {
                diagnostics::report_capture(log, snapshot, missed);
                if (output)
                    output->report(log);
                log.flush();
                next_status = now + std::chrono::seconds(1);
            }
            if (output)
                output->submit(std::make_shared<const model::Snapshot>(std::move(snapshot)));
            status(Availability::Ready, ReasonCode::None, epoch);
        } catch (const memory::ReadUnavailable&) {
            source.invalidate();
            status(Availability::Unavailable, ReasonCode::CaptureUnavailable, epoch);
            ++failures;
            if (Steady::now() >= next_status) {
                if (output)
                    output->report(log);
                log.flush();
                next_status = Steady::now() + std::chrono::seconds(1);
            }
            check_binding();
            if (retired(state) || state == memory::ProcessState::AddressSpaceChanged) {
                status(Availability::Unavailable, binding_reason(state), epoch);
                break;
            }
        }
        missed += cadence.advance(Steady::now());
        control.wait_until(cadence.next(), &frequency, cadence.rate().revision);
    }
    return state;
}
} // namespace cs2::runtime
