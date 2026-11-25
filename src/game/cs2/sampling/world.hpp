#pragma once
#include "game/cs2/sampling/common.hpp"
#include "model/world.hpp"
namespace cs2::game {
model::World sample_world(memory::IReadChannel&, const Context&);
memory::Fields bomb_fields(memory::Address);
memory::Fields projectile_fields(const EntityRef&);
model::Bomb sample_bomb(const EntitySample&);
model::Projectile sample_projectile(const EntitySample&);
} // namespace cs2::game
