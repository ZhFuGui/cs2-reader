#pragma once
#include "game/cs2/entity/directory.hpp"
namespace cs2::game {
model::World sample_world(memory::IReadChannel&, const Context&);
std::optional<model::Bomb> sample_bomb(memory::IReadChannel&, const Directory&, const EntityRef&);
std::optional<model::Projectile> sample_projectile(memory::IReadChannel&, const Directory&, const EntityRef&);
} // namespace cs2::game
