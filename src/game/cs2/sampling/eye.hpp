#pragma once
#include "game/cs2/entity/directory.hpp"
#include "model/eye.hpp"
namespace cs2::game {
std::optional<model::EyeAngleBasis> sample_eye_basis(memory::IReadChannel&, const Context&, const Directory&,
                                                     const EntityRef&);
} // namespace cs2::game
