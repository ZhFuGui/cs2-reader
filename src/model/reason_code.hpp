#pragma once
#include "reason_codes.generated.hpp"
#include <cstdint>
#include <optional>

namespace cs2::model {
struct Fault {
    ReasonCode reason;
    std::optional<std::uint32_t> os_code = {};
    bool operator==(const Fault&) const = default;
};
} // namespace cs2::model
