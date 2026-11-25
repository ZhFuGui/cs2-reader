#pragma once
#include "model/reason_code.hpp"
#include <cstdint>
#include <optional>

namespace cs2::model {
enum class Availability : unsigned { Waiting = 1, Ready = 2, Unavailable = 3, Stopping = 4 };
struct SourceStatus {
    Availability availability = Availability::Waiting;
    std::optional<std::uint64_t> epoch, last_success;
    ReasonCode reason = ReasonCode::None;
    bool operator==(const SourceStatus&) const = default;
};
} // namespace cs2::model
