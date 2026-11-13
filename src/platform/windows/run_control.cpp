#include "platform/windows/run_control.hpp"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <string>
#include <thread>

namespace cs2::platform::windows {
namespace {
std::atomic_bool console_stop{false};
BOOL WINAPI console_handler(DWORD signal) {
    if (signal != CTRL_C_EVENT && signal != CTRL_BREAK_EVENT && signal != CTRL_CLOSE_EVENT)
        return FALSE;
    console_stop.store(true);
    return TRUE;
}
} // namespace
struct RunControl::Impl {
    HANDLE event{};
    bool installed = false;
    ~Impl() {
        if (installed)
            SetConsoleCtrlHandler(console_handler, FALSE);
        if (event)
            CloseHandle(event);
    }
};
RunControl::RunControl(std::wstring_view name, bool console_controls) : impl_(std::make_unique<Impl>()) {
    if (console_controls) {
        console_stop.store(false);
        impl_->installed = SetConsoleCtrlHandler(console_handler, TRUE) != FALSE;
    }
    if (!name.empty()) {
        const std::wstring owned(name);
        impl_->event = CreateEventW(nullptr, TRUE, FALSE, owned.c_str());
        if (!impl_->event || GetLastError() == ERROR_ALREADY_EXISTS)
            throw std::runtime_error("Cannot create an exclusive named stop event");
    }
}
RunControl::~RunControl() = default;
bool RunControl::stopping() const {
    return (impl_->installed && console_stop.load()) ||
           (impl_->event && WaitForSingleObject(impl_->event, 0) == WAIT_OBJECT_0);
}
void RunControl::wait_until(std::chrono::steady_clock::time_point deadline, capture::FrequencyControl* frequency,
                            std::uint64_t revision) {
    while (!stopping() && std::chrono::steady_clock::now() < deadline) {
        const auto until = std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(50));
        if (frequency) {
            if (!frequency->wait_until(until, revision))
                return;
        } else
            std::this_thread::sleep_until(until);
    }
}
} // namespace cs2::platform::windows
