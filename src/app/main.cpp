#include "app/application.hpp"
#include "diagnostics/events.hpp"
#include "diagnostics/runtime_log.hpp"
#include <cstdlib>
#include <stdexcept>
#include <system_error>
#if defined(CS2_STEALTH)
#include <windows.h>
#endif

namespace {
int run_reader(int argc, wchar_t** argv) {
    cs2::diagnostics::RuntimeLog log;
    using cs2::model::ReasonCode;
    try {
        cs2::app::Options options;
        try {
            options = cs2::app::parse_options(argc, argv);
        } catch (const std::logic_error&) {
            cs2::diagnostics::report_fault(log.stream(), {ReasonCode::InvalidOptions}, cs2::diagnostics::Level::Error);
            return 1;
        }
        if (options.log_file) {
            try {
                log.enable_file();
            } catch (const std::system_error& error) {
                cs2::diagnostics::report_fault(
                    log.stream(), {ReasonCode::LogFileUnavailable, static_cast<std::uint32_t>(error.code().value())});
            } catch (const std::runtime_error&) {
                cs2::diagnostics::report_fault(log.stream(), {ReasonCode::LogFileUnavailable});
            }
        }
        if (options.help) {
            cs2::app::print_usage();
            return 0;
        }
        return cs2::app::run(options, log.stream());
    } catch (const std::exception&) {
        cs2::diagnostics::report_fault(log.stream(), {ReasonCode::ApplicationFailure}, cs2::diagnostics::Level::Error);
        return 1;
    }
}
} // namespace

#if defined(CS2_STEALTH)
// 标准宽字符 CRT 入口已初始化 __argc/__wargv；复用同一套参数解析，不引入第二种命令行规则。
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    return run_reader(__argc, __wargv);
}
#else
int wmain(int argc, wchar_t** argv) {
    return run_reader(argc, argv);
}
#endif
