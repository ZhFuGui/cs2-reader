#include "app/application.hpp"
#include "diagnostics/obf.hpp"
#include "diagnostics/runtime_log.hpp"
#include <cstdlib>
#include <exception>
#include <iostream>
#if defined(CS2_STEALTH)
#include <windows.h>
#endif

namespace {
int run_reader(int argc, wchar_t** argv) {
    cs2::diagnostics::RuntimeLog log;
    try {
        const auto options = cs2::app::parse_options(argc, argv);
        if (options.log_file)
            log.enable_file();
        if (options.help) {
            cs2::app::print_usage();
            return 0;
        }
        return cs2::app::run(options, log.stream());
    } catch (const std::exception& error) {
        log.stream() << OBF("Startup failed: ") << error.what() << '\n';
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
