#pragma once
#include "game/cs2/entity/directory.hpp"
namespace cs2::game {
std::optional<model::Player> sample_player(memory::IReadChannel&, const Context&, const Directory&, const EntityRef&,
                                           bool read_bones);
}
