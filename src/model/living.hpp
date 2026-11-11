#pragma once
#include "model/eye.hpp"
#include "model/recoil.hpp"
#include "model/skeleton.hpp"
#include "model/weapon.hpp"
namespace cs2::model {
struct PlayerBody {
    EntityState entity;
    std::optional<std::int32_t> armor, shots_fired;
    std::optional<bool> scoped, defusing, helmet, defuser, spotted;
    std::optional<std::array<std::uint32_t, 2>> spotted_by;
    std::optional<float> flash_duration, flash_max_alpha;
    std::optional<Angles> eye_angles;
    std::optional<Angles> input_eye_angles;
    std::optional<EyeAngleBasis> eye_basis;
    std::optional<AimPunchState> aim_punch;
    std::optional<Vec3> view_offset;
    std::optional<std::string> location;
    std::optional<EntityId> active_weapon;
    std::optional<Weapon> weapon;
    std::vector<EntityId> equipped_weapons;
    std::optional<Skeleton> skeleton;
};
struct Player {
    EntityId controller;
    std::optional<std::string> name, sanitized_name;
    std::optional<std::uint64_t> steam_id;
    std::optional<std::int32_t> connection_state, ping, score, money, bot_difficulty;
    std::optional<std::uint8_t> team;
    std::optional<bool> local, controller_reports_alive;
    std::optional<std::uint32_t> controller_reported_health, controller_reported_armor;
    std::optional<EntityId> player_pawn, observer_pawn;
    std::optional<PlayerBody> body;
};
} // namespace cs2::model
