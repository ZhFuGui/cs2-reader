#pragma once
#include "game/cs2/entity/directory.hpp"
#include "model/local.hpp"
namespace cs2::game {
struct CameraRecoilSample {
    model::EntityId pawn;
    model::CameraRecoilPath path;
    std::optional<model::CameraViewPunchState> view_punch;
};
std::optional<CameraRecoilSample> sample_camera_recoil(memory::IReadChannel&, const Context&, const Directory&,
                                                       const EntityRef& local_pawn);
} // namespace cs2::game
