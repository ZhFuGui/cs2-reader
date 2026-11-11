#pragma once
#include "game/cs2/entity/directory.hpp"
#include "model/input.hpp"

namespace cs2::game {
std::optional<model::MouseMapping> sample_mouse_mapping(memory::IReadChannel&, const Context&, const Directory&,
                                                        const EntityRef* pawn);
std::optional<bool> sample_crosshair_recoil(memory::IReadChannel&, const Context&);
} // namespace cs2::game
