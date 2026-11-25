#pragma once
#include "game/cs2/sampling/common.hpp"
#include "model/weapon.hpp"
namespace cs2::game {
memory::Fields weapon_fields(memory::Address);
model::Weapon project_weapon(const EntitySample&);
} // namespace cs2::game
