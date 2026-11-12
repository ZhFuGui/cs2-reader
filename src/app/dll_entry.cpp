#include "app/application.hpp"
#include "diagnostics/runtime_log.hpp"
#include <exception>
#include <windows.h>

namespace {
DWORD WINAPI reader_main(void*) noexcept {
    try {
        cs2::diagnostics::RuntimeLog log;
        try {
            // 使用 Reader 默认配置，不解析宿主命令行、不接管宿主控制台。
            return static_cast<DWORD>(cs2::app::run(cs2::app::Options{}, log.stream(), false));
        } catch (const std::exception& error) {
            log.stream() << "Reader stopped after an unrecoverable error: " << error.what() << '\n';
        } catch (...) {
            log.stream() << "Reader stopped after an unknown error.\n";
        }
    } catch (...) {
        // 日志自身初始化失败时也只退出当前线程，不让 C++ 异常越过线程入口。
    }
    return 1;
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // 只支持正常加载，模块随宿主存活，避免工作线程执行期间被 FreeLibrary 卸载。
        // 不调用 DisableThreadLibraryCalls：/MT 的 CRT 仍需要线程通知。
        HMODULE pinned{};
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(module), &pinned)) {
            // 不等待线程；正常加载下，线程在本次加载初始化返回后才进入 reader_main。
            // 引导线程直接成为 Reader 主线程，不再创建额外的常驻管理线程。
            if (const auto thread = CreateThread(nullptr, 0, reader_main, nullptr, 0, nullptr))
                CloseHandle(thread);
        }
    }
    // 宿主退出时由系统回收资源，不能在加载器锁内 join 或执行复杂清理。
    // 启动失败不返回 FALSE，避免静态加载失败导致整个宿主进程退出。
    return TRUE;
}
