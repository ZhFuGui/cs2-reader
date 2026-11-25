#pragma once
#include "runtime/runner.hpp"
#include <chrono>
#include <iosfwd>

namespace cs2::runtime {
using Steady = std::chrono::steady_clock;

inline bool retired(memory::ProcessState state) noexcept {
    return state == memory::ProcessState::Exited || state == memory::ProcessState::Changed;
}

// 由运行线程独占。绑定和采集之间共享计数、停止条件及状态发布，不借用通道。
struct RunState {
    const Limits& limits;
    const Environment& environment;
    std::ostream& log;
    const Steady::time_point started = Steady::now();
    Steady::time_point next_status = started;
    std::uint64_t sequence = 0, epoch = 0, failures = 0, missed = 0;
    std::optional<model::SourceStatus> last_status = {};

    bool running() const;
    void status(model::Availability, model::ReasonCode = model::ReasonCode::None, std::optional<std::uint64_t> = {});
    void wait_for_source();
    void retry(model::ReasonCode, std::optional<std::uint64_t> = {});
    memory::ProcessState capture(Binding&, capture::ISource&);
};
} // namespace cs2::runtime
