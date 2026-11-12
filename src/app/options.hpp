#pragma once
#include "memory/backend.hpp"
#include <filesystem>
#include <optional>
#include <string>
namespace cs2::app {
struct Options {
    memory::AccessMode access = memory::AccessMode::ReadOnly;
    std::optional<std::uint32_t> pid;
    std::optional<double> wait_seconds;
    std::optional<double> duration_seconds;
    std::uint64_t samples = 0;
    std::size_t workers = 2;
    double hz = 90;
    bool bones = true, help = false;
    bool service = true;
    bool log_file = false;
    std::uint16_t port = 13202;
    std::string bind_address = "127.0.0.1";
    std::size_t maximum_clients = 8;
    std::wstring stop_event;
    std::filesystem::path output;
};
Options parse_options(int argc, wchar_t** argv);
void print_usage();
} // namespace cs2::app
