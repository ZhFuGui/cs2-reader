#pragma once
#include "capture/clock.hpp"
#include "game/cs2/entity/directory.hpp"
#include "model/accuracy.hpp"
#include <span>
namespace cs2::game {
class AccuracyRules;
struct PawnVelocity;
model::WeaponAccuracyEvaluation sample_weapon_accuracy(memory::IReadChannel&, const Context&, const Directory&,
                                                       const EntityRef&, const capture::IClock&,
                                                       const AccuracyRules*, std::span<const PawnVelocity>);
} // namespace cs2::game
