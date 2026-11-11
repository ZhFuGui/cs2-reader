#pragma once
#include "game/cs2/eye/observation.hpp"
#include "game/cs2/session/context.hpp"
#include "model/eye.hpp"
namespace cs2::game::eye {
std::optional<model::EyeAngleBasis> history_basis(Observation&, const Context&, memory::Address pawn,
                                                  std::int32_t native_context);
} // namespace cs2::game::eye
