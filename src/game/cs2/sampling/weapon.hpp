#pragma once
#include "capture/clock.hpp"
#include "game/cs2/sampling/common.hpp"
namespace cs2::game {
class AccuracyRules;
memory::Fields weapon_fields(memory::Address);
model::Weapon sample_weapon(memory::IReadChannel&, const Context&, const Directory&, const EntitySample&,
                            const capture::IClock&, const AccuracyRules* = nullptr);
} // namespace cs2::game
