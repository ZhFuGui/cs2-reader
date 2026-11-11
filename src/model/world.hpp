#pragma once
#include "model/types.hpp"
namespace cs2::model {
enum class MapStatus : std::uint32_t {
    Unknown = 0, Available = 1, NotInLevel = 2, SourceUnavailable = 3, Transition = 4, Unsupported = 5
};
struct MapIdentity {
    MapStatus status = MapStatus::Unknown;
    std::uint64_t generation = 0;
    std::optional<std::string> name, resource_path;
};
struct Bomb {
    EntityState entity;
    std::optional<bool> ticking, defused, being_defused, exploded;
    std::optional<std::int32_t> site;
    std::optional<float> explode_at, timer_length, defuse_at, defuse_length;
    std::optional<EntityId> defuser;
};
struct Projectile {
    EntityState entity;
    std::optional<Vec3> initial_position, initial_velocity;
    std::optional<std::int32_t> bounces, explode_tick, smoke_tick;
    std::optional<bool> exploded, smoke_started;
    std::optional<float> spawn_time;
};
struct World {
    std::optional<bool> freeze_period, warmup, match_started, bomb_planted, bomb_dropped;
    std::optional<std::int32_t> round_time, rounds_played, round_win_status, phase;
    std::optional<float> round_started_at;
    std::optional<Vec3> minimap_min, minimap_max;
    std::optional<MapIdentity> map_identity;
};
} // namespace cs2::model
