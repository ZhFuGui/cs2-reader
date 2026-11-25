#pragma once
#include "event_ids.generated.hpp"
#include "model/reason_code.hpp"
#include <iosfwd>

namespace cs2::diagnostics {
enum class Level { Info, Warning, Error };

std::ostream& event(std::ostream&, EventId, Level = Level::Info);
void report_fault(std::ostream&, model::Fault, Level = Level::Warning);
} // namespace cs2::diagnostics
