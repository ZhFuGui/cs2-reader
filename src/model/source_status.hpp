#pragma once
#include <cstdint>
#include <optional>
#include <string>

namespace cs2::model {
enum class Availability : unsigned { Waiting = 1, Ready = 2, Unavailable = 3, Stopping = 4 };
struct SourceStatus {
    Availability availability = Availability::Waiting;
    std::optional<std::uint64_t> epoch, last_success;
    std::string reason = "WAITING_FOR_PROCESS";
};
} // namespace cs2::model
