#pragma once
#include <cstdint>

namespace cs2::model {
struct Coverage {
    std::uint64_t slots = 0, empty = 0, invalid = 0, unreadable = 0, changed = 0, unknown = 0, classified = 0;
    std::int32_t reported_highest_index = -1;
    std::uint32_t allocated_chunks = 0;
};
} // namespace cs2::model
