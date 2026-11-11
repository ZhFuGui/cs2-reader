#pragma once
#include "game/cs2/entity/directory.hpp"
#include "model/recoil.hpp"

namespace cs2::game {
std::optional<model::AimPunchState> sample_aim_punch(memory::IReadChannel&, const Context&, const Directory&,
                                                     const EntityRef&, memory::Address service);
std::optional<model::CameraViewPunchState> sample_camera_view_punch(memory::IReadChannel&, const Context&,
                                                                    const Directory&, const EntityRef&,
                                                                    memory::Address service);
} // namespace cs2::game
