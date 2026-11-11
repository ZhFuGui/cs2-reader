#pragma once
#include "game/cs2/entity/directory.hpp"
#include "capture/clock.hpp"
namespace cs2::game {
class AccuracyRules;
std::optional<model::Weapon> sample_weapon(memory::IReadChannel&, const Context&, const Directory&, const EntityRef&,
                                         const capture::IClock&, const AccuracyRules* = nullptr);
}
