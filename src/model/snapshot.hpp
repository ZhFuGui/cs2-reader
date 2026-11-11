#pragma once
#include "memory/read_channel.hpp"
#include "model/living.hpp"
#include "model/local.hpp"
#include "model/world.hpp"
namespace cs2::model {
struct Coverage {
    std::uint64_t slots = 0, empty = 0, invalid = 0, unreadable = 0, changed = 0, unknown = 0, classified = 0;
    std::int32_t reported_highest_index = -1;
    std::uint32_t allocated_chunks = 0;
};
struct Snapshot {
    std::uint64_t sequence = 0, epoch = 0, qpc_frequency = 0;
    std::uint32_t process_id = 0, game_build = 0;
    double capture_target_hz = 0; // 本批开采时冻结的目标，0表示调用方未提供；不冒充实测FPS。
    Interval interval;
    Coverage coverage;
    memory::ReadMetrics reads;
    bool directory_verified = false;
    std::vector<Player> players;
    std::vector<Weapon> weapons;
    std::vector<Bomb> bombs;
    std::vector<Projectile> projectiles;
    std::vector<EntityState> world_objects;
    Local local;
    World world;
};
} // namespace cs2::model
