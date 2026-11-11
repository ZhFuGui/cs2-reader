#include "app/options.hpp"
#include "transport/ipv4.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
namespace cs2::app {
namespace {
std::uint64_t integer(const std::wstring& input) {
    if (input.empty() || input.front() == L'-')
        throw std::runtime_error("Expected a non-negative integer");
    std::size_t end = 0;
    const auto result = std::stoull(input, &end);
    if (end != input.size())
        throw std::runtime_error("Invalid integer argument");
    return result;
}
double number(const std::wstring& input) {
    std::size_t end = 0;
    const auto result = std::stod(input, &end);
    if (end != input.size() || !std::isfinite(result))
        throw std::runtime_error("Invalid numeric argument");
    return result;
}
} // namespace
Options parse_options(int argc, wchar_t** argv) {
    Options options;
    options.workers = std::max<std::size_t>(1, std::min<std::size_t>(4, std::thread::hardware_concurrency()));
    for (int i = 1; i < argc; ++i) {
        const std::wstring key = argv[i];
        auto next = [&]() -> std::wstring {
            if (++i >= argc)
                throw std::runtime_error("Missing option value");
            return argv[i];
        };
        if (key == L"--help" || key == L"-h")
            options.help = true;
        else if (key == L"--backend") {
            const auto value = next();
            if (value == L"r3")
                options.backend = memory::BackendKind::R3;
            else if (value == L"hyperv")
                options.backend = memory::BackendKind::HyperV;
            else
                throw std::runtime_error("Backend must be r3 or hyperv");
        } else if (key == L"--pid") {
            const auto value = integer(next());
            if (!value || value > UINT32_MAX)
                throw std::runtime_error("PID is out of range");
            options.pid = static_cast<std::uint32_t>(value);
        } else if (key == L"--workers") {
            const auto value = integer(next());
            if (!value || value > 256)
                throw std::runtime_error("Workers must be between 1 and 256");
            options.workers = static_cast<std::size_t>(value);
        } else if (key == L"--hz") {
            options.hz = number(next());
            if (options.hz < 1 || options.hz > 300)
                throw std::runtime_error("Frequency must be between 1 and 300 Hz");
        } else if (key == L"--samples")
            options.samples = integer(next());
        else if (key == L"--once")
            options.samples = 1;
        else if (key == L"--wait-seconds") {
            const auto value = number(next());
            if (value < 0)
                throw std::runtime_error("Wait duration cannot be negative");
            options.wait_seconds = value;
        } else if (key == L"--duration") {
            const auto value = number(next());
            if (value <= 0)
                throw std::runtime_error("Duration must be positive");
            options.duration_seconds = value;
        } else if (key == L"--output")
            options.output = next();
        else if (key == L"--no-bones")
            options.bones = false;
        else if (key == L"--read-write")
            options.access = memory::AccessMode::ReadWrite;
        else if (key == L"--no-service")
            options.service = false;
        else if (key == L"--log-file")
            options.log_file = true;
        else if (key == L"--bind") {
            const auto value = next();
            options.bind_address.clear();
            for (const auto character : value) {
                if (character > 127)
                    throw std::runtime_error("Bind address must be a numeric IPv4 address");
                options.bind_address.push_back(static_cast<char>(character));
            }
            if (!transport::parse_ipv4(options.bind_address))
                throw std::runtime_error("Bind address must be a numeric IPv4 address");
        } else if (key == L"--port") {
            const auto value = integer(next());
            if (!value || value > UINT16_MAX)
                throw std::runtime_error("Port must be between 1 and 65535");
            options.port = static_cast<std::uint16_t>(value);
        } else if (key == L"--max-clients") {
            const auto value = integer(next());
            if (!value || value > 60)
                throw std::runtime_error("Client budget must be between 1 and 60");
            options.maximum_clients = static_cast<std::size_t>(value);
        } else if (key == L"--stop-event")
            options.stop_event = next();
        else
            throw std::runtime_error("Unknown startup option");
    }
    return options;
}
void print_usage() {
#if !defined(CS2_STEALTH)
    std::cout << "reader --backend r3|hyperv [--pid N] [--hz 90] [--workers 4]\n"
                 "           [--once | --samples N] [--duration SECONDS] [--wait-seconds N] [--output FILE.jsonl]\n"
                 "           [--no-bones] [--read-write] [--no-service] [--port 13202] [--max-clients 8]\n"
                 "           [--bind 127.0.0.1 | --bind LAN_IPV4 | --bind 0.0.0.0] [--stop-event NAME] [--log-file]\n"
                 "The backend is fixed for this process. Default: R3, read-only, automatic name discovery.\n"
                 "Read-only subscriptions use --bind (default 127.0.0.1). No remote write or backend switch commands.\n"
                 "Subscriptions may request 1..300 Hz at any time; the highest active request drives capture.\n"
                 "Runtime logs stay in a 1MiB memory ring; --log-file writes a random log in %TEMP%.\n"
                 "--output explicitly exports snapshot data, separately from runtime logs.\n"
                 "Ctrl+C or the named stop event stops.\n";
#endif
    // CS2_STEALTH：Windows 子系统下没有控制台，帮助文本既不可见也不该留在镜像里。
}
} // namespace cs2::app
