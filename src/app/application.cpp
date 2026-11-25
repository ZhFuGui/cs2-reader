#include "app/application.hpp"
#include "app/composition.hpp"
#include "app/outputs.hpp"
#include "cs2_reader/v2/schema.hpp"
#include "diagnostics/events.hpp"
#include "diagnostics/obf.hpp"
#include "diagnostics/snapshot_sink.hpp"
#include "game/cs2/capture/factory.hpp"
#include "platform/windows/qpc_clock.hpp"
#include "platform/windows/run_control.hpp"
#include "runtime/runner.hpp"
#include <ostream>

namespace cs2::app {
int run(const Options& options, std::ostream& log, bool console_controls) {
    platform::windows::RunControl control(options.stop_event, console_controls);
    constexpr auto target_name = memory::process_name(L"cs2.exe");
    const runtime::Target target{target_name, options.pid, options.access};
    const auto backend = create_backend(target.name, OBF("cs2.exe")); // 唯一后端选择点；运行期间不切换。
    const platform::windows::QpcClock clock;
    capture::FrequencyControl frequency(options.hz);
    const game::SourceFactory sources(options.workers, options.bones);
    std::unique_ptr<Outputs> output;
    if (options.service || !options.output.empty()) {
        try {
            output = std::make_unique<Outputs>(options, clock, frequency);
        } catch (const diagnostics::SnapshotOutputFailed& error) {
            diagnostics::report_fault(log, {model::ReasonCode::SnapshotExportFailed, error.os_code},
                                      diagnostics::Level::Error);
            return 1;
        }
    }
    diagnostics::event(log, diagnostics::EventId::Started)
        << " protocol_version=" << cs2_reader::v2::protocol_version << " workers=" << options.workers
        << " target_hz=" << options.hz << '\n';
    const runtime::Limits limits{options.wait_seconds, options.duration_seconds, options.samples};
    const runtime::Environment environment{*backend,    backend->process_catalog(), sources, clock, frequency, control,
                                           output.get()};
    return runtime::run(target, limits, environment, log);
}
} // namespace cs2::app
