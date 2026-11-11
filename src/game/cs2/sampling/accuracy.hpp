#pragma once
#include "capture/clock.hpp"
#include "game/cs2/entity/directory.hpp"
#include "model/accuracy.hpp"
namespace cs2::game {
class AccuracyRules;
model::WeaponAccuracyEvaluation sample_weapon_accuracy(memory::IReadChannel&, const Context&, const Directory&,
                                                       const EntityRef&, const capture::IClock&, const AccuracyRules* = nullptr);
}
