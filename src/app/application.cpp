#include "app/application.hpp"
#include "app/composition.hpp"
#include "capture/collector.hpp"
#include "diagnostics/snapshot_sink.hpp"
#include "diagnostics/obf.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "platform/windows/process_catalog.hpp"
#include "platform/windows/qpc_clock.hpp"
#include "synchronization/v1/service.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace cs2::app {
namespace {
std::atomic_bool stop{false};
BOOL WINAPI console_handler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) {
        stop.store(true);
        return TRUE;
    }
    return FALSE;
}
std::optional<memory::ProcessIdentity> target(const Options& options) {
    using namespace platform::windows;
    constexpr auto name = process_name(L"cs2.exe");
    return find_process(name, options.pid);
}
void pause_until(std::chrono::steady_clock::time_point deadline, capture::FrequencyControl* frequency = nullptr,
                 std::uint64_t revision = 0) {
    while (!stop.load() && std::chrono::steady_clock::now() < deadline) {
        const auto until = std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(50));
        if (frequency) {
            if (!frequency->wait_until(until, revision))
                return;
        } else
            std::this_thread::sleep_until(until);
    }
}
} // namespace
int run(const Options& options, std::ostream& log) {
    stop = false;
    SetConsoleCtrlHandler(console_handler, TRUE);
    struct Handler {
        ~Handler() { SetConsoleCtrlHandler(console_handler, FALSE); }
    } handler;
    const auto backend = create_backend(); // 唯一选择点；后续没有切换或回退入口。
    const platform::windows::QpcClock clock;
    capture::FrequencyControl frequency(options.hz);
    struct StopEvent {
        HANDLE value{};
        ~StopEvent() {
            if (value)
                CloseHandle(value);
        }
    } stop_event;
    if (!options.stop_event.empty()) {
        stop_event.value = CreateEventW(nullptr, TRUE, FALSE, options.stop_event.c_str());
        if (!stop_event.value || GetLastError() == ERROR_ALREADY_EXISTS)
            throw std::runtime_error("Cannot create an exclusive named stop event");
    }
    using synchronization::v1::Availability;
    std::unique_ptr<synchronization::v1::SubscriptionService> service;
    if (options.service) {
        service = std::make_unique<synchronization::v1::SubscriptionService>(
            synchronization::v1::ServiceOptions{options.port, options.maximum_clients, options.hz,
                                                options.bones,
                                                options.bind_address},
            clock, &frequency);
        log << "Read-only subscription: " << options.bind_address << ':' << options.port << " / protocol v1\n";
    }
    log << "Backend: hyper-mem"
              << " (fixed), access=" << (options.access == memory::AccessMode::ReadOnly ? "read-only" : "read-write")
              << "\nOffset profile: build " << game::offsets::expected_build() << " / " << game::offsets::generated_at()
              << "\nWorkers: " << options.workers << ", target: " << options.hz << " Hz\n";
    std::unique_ptr<diagnostics::SnapshotSink> sink;
    if (!options.output.empty())
        sink = std::make_unique<diagnostics::SnapshotSink>(options.output);
    std::uint64_t sequence = 0, epoch = 0, failures = 0, missed = 0;
    const auto wait_started = std::chrono::steady_clock::now();
    const auto running = [&] {
        return !stop.load() && (!stop_event.value || WaitForSingleObject(stop_event.value, 0) != WAIT_OBJECT_0) &&
               (!options.samples || sequence < options.samples) &&
               (!options.duration_seconds ||
                std::chrono::duration<double>(std::chrono::steady_clock::now() - wait_started).count() <
                    *options.duration_seconds);
    };
    auto next_status = wait_started;
    std::unique_ptr<memory::ISession> session;
    std::optional<memory::ProcessIdentity> process;
    bool discard_session = false;
    while (running()) {
        if (discard_session) {
            session.reset(); // 上一轮 Collector 已经析构，再释放其引用的会话。
            discard_session = false;
        }
        if (!session) {
            process = target(options); // 同一快照可供选择、内核自证和绑定复用。
            if (!process) {
                try {
                    // 未发现游戏时只轮询 hypervisor；出现候选才触发一次原生快照。
                    const auto hint = backend->find_process_hint(OBF("cs2.exe").c_str());
                    if (hint && (!options.pid || hint->pid == *options.pid)) {
                        platform::windows::invalidate_process_catalog();
                        process = target(options);
                    }
                } catch (const std::exception& error) {
                    if (std::chrono::steady_clock::now() >= next_status) {
                        log << "Process discovery unavailable: " << error.what() << '\n';
                        next_status = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                    }
                }
            }
        }
        if (!session && !process) {
            if (service)
                service->unavailable(Availability::Waiting, "WAITING_FOR_PROCESS");
            const auto now = std::chrono::steady_clock::now();
            if (options.wait_seconds &&
                std::chrono::duration<double>(now - wait_started).count() >= *options.wait_seconds) {
                log << "Target process was not found.\n";
                return 3;
            }
            if (now >= next_status) {
                log << "Waiting for the configured process...\n";
                next_status = now + std::chrono::seconds(3);
            }
            pause_until(now + std::chrono::milliseconds(500));
            continue;
        }
        if (!session) {
            try {
                session = backend->attach(*process, options.access);
            } catch (const std::exception& error) {
                if (service) service->unavailable(Availability::Unavailable, "PROCESS_BINDING_UNAVAILABLE");
                // 仅当内核线索确认身份已变，才令原生快照失效；绑定故障不反复枚举。
                try {
                    const auto hint = backend->find_process_hint(OBF("cs2.exe").c_str());
                    if (!hint || *hint != *process) platform::windows::invalidate_process_catalog();
                } catch (const std::exception&) {}
                if (std::chrono::steady_clock::now() >= next_status) {
                    log << "Process binding unavailable: " << error.what() << '\n';
                    next_status = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                }
                pause_until(std::chrono::steady_clock::now() + std::chrono::milliseconds(500));
                continue;
            }
        }
        auto bootstrap = session->open_reader();
        game::Context context;
        try {
            context = game::open_context(*bootstrap, *session);
        } catch (const game::ContextPending& error) {
            if (service)
                service->unavailable(Availability::Unavailable, "GAME_CONTEXT_PENDING");
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_status) {
                log << error.what() << '\n';
                next_status = now + std::chrono::seconds(3);
            }
            if (session->state() != memory::ProcessState::Current) {
                session.reset();
                platform::windows::invalidate_process_catalog();
            }
            pause_until(now + std::chrono::milliseconds(500));
            continue;
        }
        ++epoch;
        capture::Collector collector(*session, context, clock, options.workers, options.bones);
        const auto native_queries = platform::windows::process_catalog_stats();
        log << "Attached PID " << process->pid << ", build " << context.build << ", epoch " << epoch
            << " native_length_queries=" << native_queries.length_queries
            << " native_snapshot_queries=" << native_queries.snapshot_queries << '\n';
        log.flush();
        auto rate = frequency.current();
        auto period = std::chrono::nanoseconds(static_cast<std::int64_t>(1e9 / rate.hz));
        auto next = std::chrono::steady_clock::now();
        auto last_alive = next;
        next_status = next;
        auto invalid_state = memory::ProcessState::Current;
        while (running()) {
            const auto now = std::chrono::steady_clock::now();
            const auto requested = frequency.current();
            if (requested.revision != rate.revision) {
                rate = requested;
                period = std::chrono::nanoseconds(static_cast<std::int64_t>(1e9 / rate.hz));
                next = now; // 调频重新开始时间线，不补采旧周期。
            }
            if (now - last_alive >= std::chrono::seconds(1)) {
                invalid_state = session->state();
                if (invalid_state != memory::ProcessState::Current) {
                    if (service)
                        service->unavailable(Availability::Unavailable,
                            invalid_state == memory::ProcessState::Exited ? "PROCESS_EXITED" : "PROCESS_BINDING_UNAVAILABLE", epoch);
                    break;
                }
                last_alive = now;
            }
            try {
                auto snapshot = collector.capture(sequence + 1, epoch);
                snapshot.capture_target_hz = rate.hz;
                ++sequence;
                if (now >= next_status || options.samples == 1) {
                    std::size_t bodies = 0, bone_points = 0;
                    for (const auto& player : snapshot.players)
                        if (player.body) {
                            ++bodies;
                            if (player.body->skeleton)
                                bone_points += player.body->skeleton->points.size();
                        }
                    const double elapsed_ms =
                        1000.0 * static_cast<double>(snapshot.interval.finished_qpc - snapshot.interval.started_qpc) /
                        snapshot.qpc_frequency;
                    log << "seq=" << sequence << " target_hz=" << rate.hz << " slots=" << snapshot.coverage.slots
                              << " players=" << snapshot.players.size() << " bodies=" << bodies
                              << " weapons=" << snapshot.weapons.size() << " bombs=" << snapshot.bombs.size()
                              << " projectiles=" << snapshot.projectiles.size() << " bones=" << bone_points
                              << " local=" << snapshot.local.pawn.has_value() << " read_calls=" << snapshot.reads.calls
                              << " read_bytes=" << snapshot.reads.requested
                              << " read_failures=" << snapshot.reads.failures
                              << " hc_single=" << snapshot.reads.hypercall_single << " hc_batch=" << snapshot.reads.hypercall_batch
                              << " batch_items=" << snapshot.reads.batch_items << " batch_errors=" << snapshot.reads.batch_errors
                              << " item_splits=" << snapshot.reads.batch_item_splits << " byte_splits=" << snapshot.reads.batch_byte_splits
                              << " capture_ms=" << elapsed_ms
                              << " missed=" << missed << '\n';
                    if (service) {
                        const auto stats = service->stats();
                        log << "tcp_clients=" << stats.connected << " encoded=" << stats.encoded
                                  << " sent=" << stats.sent << " pending_coalesced=" << stats.pending_coalesced
                                  << " encoder_coalesced=" << stats.encoder_coalesced;
                        if (!stats.error.empty())
                            log << " service_error=" << stats.error;
                        log << '\n';
                    }
                    log.flush();
                    next_status = now + std::chrono::seconds(1);
                }
                if (service || sink) {
                    auto shared = std::make_shared<const model::Snapshot>(std::move(snapshot));
                    if (service)
                        service->submit(shared);
                    if (sink)
                        sink->submit(std::move(shared));
                }
                if (sink) {
                    if (const auto error = sink->error(); !error.empty())
                        log << error << '\n';
                }
            } catch (const std::exception& error) {
                collector.invalidate_map();
                if (service)
                    service->unavailable(Availability::Unavailable, "CAPTURE_UNAVAILABLE", epoch);
                ++failures;
                if (std::chrono::steady_clock::now() >= next_status) {
                    log << "Capture unavailable: " << error.what() << '\n';
                    next_status = std::chrono::steady_clock::now() + std::chrono::seconds(1);
                }
                invalid_state = session->state();
                if (invalid_state != memory::ProcessState::Current) break;
            }
            next += period;
            const auto finished = std::chrono::steady_clock::now();
            if (finished >= next + period) {
                const auto skipped = (finished - next) / period;
                missed += skipped;
                next += period * skipped;
            }
            pause_until(next, &frequency, rate.revision);
        }
        if (running()) {
            discard_session = true;
            platform::windows::invalidate_process_catalog();
        }
        if (options.pid && running() && invalid_state != memory::ProcessState::Unreadable) {
            log << "The selected process instance has exited or changed.\n";
            return 3;
        }
    }
    if (service)
        service->unavailable(Availability::Stopping, "USER_STOP",
                             epoch ? std::optional<std::uint64_t>(epoch) : std::nullopt);
    service.reset();
    const auto native_queries = platform::windows::process_catalog_stats();
    log << "Stopped. captures=" << sequence << " failed=" << failures << " missed_opportunities=" << missed
        << " native_length_queries=" << native_queries.length_queries
        << " native_snapshot_queries=" << native_queries.snapshot_queries;
    if (sink)
        log << " output_coalesced=" << sink->coalesced();
    log << '\n';
    return 0;
}
} // namespace cs2::app
