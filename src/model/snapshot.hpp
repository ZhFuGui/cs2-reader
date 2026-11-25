#pragma once
#include "diagnostics/read_metrics.hpp"
#include "model/coverage.hpp"
#include "model/living.hpp"
#include "model/local.hpp"
#include "model/world.hpp"
namespace cs2::model {
struct Snapshot {
    std::uint64_t sequence = 0, epoch = 0, qpc_frequency = 0;
    std::uint32_t process_id = 0, game_build = 0;
    double capture_target_hz = 0; // 本批开采时冻结的目标，0表示调用方未提供；不冒充实测FPS。
    Interval interval;
    Coverage coverage;
    diagnostics::ReadMetrics reads;
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
