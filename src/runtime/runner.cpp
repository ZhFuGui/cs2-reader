#include "runtime/runner.hpp"
#include "capture/cadence.hpp"
#include "diagnostics/capture_report.hpp"
#include <ostream>
#include <system_error>

namespace cs2::runtime {
int run(const Target& target, const Limits& limits, const Environment& environment, std::ostream& log) {
    const auto& [backend, catalog, factory, clock, frequency, control, output] = environment;
    using model::Availability;
    using Steady = std::chrono::steady_clock;
    std::uint64_t sequence = 0, epoch = 0, failures = 0, missed = 0;
    const auto started = Steady::now();
    auto next_status = started;
    const auto running = [&] {
        return !control.stopping() && (!limits.samples || sequence < limits.samples) &&
               (!limits.duration_seconds ||
                std::chrono::duration<double>(Steady::now() - started).count() < *limits.duration_seconds);
    };
    const auto unavailable = [&](Availability state, const char* reason, std::optional<std::uint64_t> value = {}) {
        if (output)
            output->unavailable(state, reason, value);
    };
    const auto retry = [&](const char* reason, const char* message, std::optional<std::uint64_t> value = {}) {
        unavailable(Availability::Unavailable, reason, value);
        const auto now = Steady::now();
        if (now >= next_status) {
            log << reason << ": " << message << '\n';
            next_status = now + std::chrono::seconds(3);
        }
        control.wait_until(now + std::chrono::milliseconds(500));
    };
    const auto selected_gone = [&](memory::ProcessState state) {
        return target.pid && running() &&
               (state == memory::ProcessState::Exited || state == memory::ProcessState::Changed);
    };
    const auto selected_exit = [&] {
        log << "The selected process instance has exited or changed.\n";
        return 3;
    };
    Binding binding(backend, catalog, target);
    while (running()) {
        memory::ISession* session = nullptr;
        try {
            session = binding.acquire();
        } catch (const BindingPending& error) {
            retry(error.stage == BindingPending::Stage::Discovery ? "PROCESS_DISCOVERY_UNAVAILABLE"
                                                                  : "PROCESS_BINDING_UNAVAILABLE",
                  error.what());
            continue;
        }
        if (!session) {
            unavailable(Availability::Waiting, "WAITING_FOR_PROCESS");
            const auto now = Steady::now();
            if (limits.wait_seconds && std::chrono::duration<double>(now - started).count() >= *limits.wait_seconds) {
                log << "Target process was not found.\n";
                return 3;
            }
            if (now >= next_status) {
                log << "Waiting for the configured process...\n";
                next_status = now + std::chrono::seconds(3);
            }
            control.wait_until(now + std::chrono::milliseconds(500));
            continue;
        }
        std::unique_ptr<capture::ISource> source;
        try {
            source = factory.open(*session, clock);
            if (!source)
                throw std::logic_error("Capture factory did not provide a source");
        } catch (const memory::ReadUnavailable& error) {
            if (selected_gone(binding.discard_if_gone()))
                return selected_exit();
            retry("GAME_CONTEXT_PENDING", error.what());
            continue;
        } catch (const std::system_error& error) {
            if (error.code() != std::errc::resource_unavailable_try_again &&
                error.code() != std::errc::not_enough_memory)
                throw;
            retry("CAPTURE_WORKERS_UNAVAILABLE", error.what());
            continue;
        } catch (const std::runtime_error& error) {
            const auto state = binding.discard_if_gone();
            if (state == memory::ProcessState::Current)
                throw;
            if (selected_gone(state))
                return selected_exit();
            retry("GAME_CONTEXT_PENDING", error.what());
            continue;
        }
        ++epoch;
        const auto native_queries = catalog.stats();
        log << "Attached PID " << session->identity().pid << ", build " << source->build() << ", epoch " << epoch
            << " native_length_queries=" << native_queries.length_queries
            << " native_snapshot_queries=" << native_queries.snapshot_queries << '\n';
        log.flush();
        capture::Cadence cadence(frequency.current(), Steady::now());
        auto last_alive = Steady::now();
        next_status = last_alive;
        auto state = memory::ProcessState::Current;
        while (running()) {
            const auto now = Steady::now();
            cadence.configure(frequency.current(), now);
            if (state == memory::ProcessState::Unreadable || now - last_alive >= std::chrono::seconds(1)) {
                state = binding.observe();
                if (state == memory::ProcessState::Unreadable) {
                    retry("PROCESS_BINDING_UNAVAILABLE", "Bound process identity is temporarily unreadable", epoch);
                    continue;
                }
                if (state != memory::ProcessState::Current) {
                    unavailable(Availability::Unavailable,
                                state == memory::ProcessState::Exited ? "PROCESS_EXITED"
                                                                      : "PROCESS_BINDING_UNAVAILABLE",
                                epoch);
                    break;
                }
                last_alive = now;
            }
            try {
                auto snapshot = source->capture(sequence + 1, epoch);
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
            } catch (const memory::ReadUnavailable& error) {
                source->invalidate();
                unavailable(Availability::Unavailable, "CAPTURE_UNAVAILABLE", epoch);
                ++failures;
                if (Steady::now() >= next_status) {
                    log << "Capture unavailable: " << error.what() << '\n';
                    next_status = Steady::now() + std::chrono::seconds(1);
                }
                state = binding.observe();
                if (state == memory::ProcessState::Exited || state == memory::ProcessState::Changed)
                    break;
            }
            missed += cadence.advance(Steady::now());
            control.wait_until(cadence.next(), &frequency, cadence.rate().revision);
        }
        source.reset(); // 包含 worker 的采集器先析构，才能释放其借用的会话。
        if (running())
            binding.release();
        if (target.pid && running() && state != memory::ProcessState::Unreadable)
            return selected_exit();
    }
    unavailable(Availability::Stopping, "USER_STOP", epoch ? std::optional<std::uint64_t>(epoch) : std::nullopt);
    const auto native_queries = catalog.stats();
    log << "Stopped. captures=" << sequence << " failed=" << failures << " missed_opportunities=" << missed
        << " native_length_queries=" << native_queries.length_queries
        << " native_snapshot_queries=" << native_queries.snapshot_queries;
    if (output)
        if (const auto count = output->coalesced())
            log << " output_coalesced=" << *count;
    log << '\n';
    return 0;
}
} // namespace cs2::runtime
