#include "app/application.hpp"
#include "app/composition.hpp"
#include "app/outputs.hpp"
#include "diagnostics/obf.hpp"
#include "game/cs2/capture/factory.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "platform/windows/process_catalog.hpp"
#include "platform/windows/qpc_clock.hpp"
#include "platform/windows/run_control.hpp"
#include "runtime/runner.hpp"
#include <ostream>

namespace cs2::app {
int run(const Options& options, std::ostream& log, bool console_controls) {
    platform::windows::RunControl control(options.stop_event, console_controls);
    const auto backend = create_backend(); // 唯一后端选择点；运行期间不切换。
    const platform::windows::QpcClock clock;
    capture::FrequencyControl frequency(options.hz);
    const game::SourceFactory sources(options.workers, options.bones);
    std::unique_ptr<Outputs> output;
    if (options.service || !options.output.empty())
        output = std::make_unique<Outputs>(options, clock, frequency);
    if (options.service)
        log << "Read-only subscription: " << options.bind_address << ':' << options.port << " / protocol v1\n";
    log << "Backend: hyper-mem"
        << " (fixed), access=" << (options.access == memory::AccessMode::ReadOnly ? "read-only" : "read-write")
        << "\nOffset profile: build " << game::offsets::expected_build() << " / " << game::offsets::generated_at()
        << "\nWorkers: " << options.workers << ", target: " << options.hz << " Hz\n";
    constexpr auto target_name = memory::process_name(L"cs2.exe");
    const runtime::Target target{target_name, OBF("cs2.exe"), options.pid, options.access};
    const runtime::Limits limits{options.wait_seconds, options.duration_seconds, options.samples};
    const runtime::Environment environment{
        *backend, platform::windows::process_catalog(), sources, clock, frequency, control, output.get()};
    return runtime::run(target, limits, environment, log);
}
} // namespace cs2::app
