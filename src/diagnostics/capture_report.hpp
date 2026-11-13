#pragma once
#include "model/snapshot.hpp"
#include <iosfwd>

namespace cs2::diagnostics {
void report_capture(std::ostream&, const model::Snapshot&, std::uint64_t missed);
} // namespace cs2::diagnostics
