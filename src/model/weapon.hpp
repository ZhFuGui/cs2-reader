#pragma once
#include "model/types.hpp"
#include "model/accuracy.hpp"
namespace cs2::model {
struct Weapon {
    EntityState entity;
    std::optional<std::uint16_t> definition_index;
    std::optional<std::string> custom_name;
    std::optional<std::int32_t> clip, secondary_clip, reserve_ammo, paint_kit, stattrak;
    std::optional<float> wear, accuracy_penalty, recoil_index;
    std::optional<bool> reloading, silenced, burst_mode;
    std::optional<WeaponAccuracyEvaluation> accuracy_evaluation;
};
} // namespace cs2::model
