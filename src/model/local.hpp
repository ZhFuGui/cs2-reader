#pragma once
#include "model/input.hpp"
#include "model/recoil.hpp"
#include "model/types.hpp"
namespace cs2::model {
enum class CameraRecoilPath : std::uint32_t { Player = 1, ObservedPlayer = 2 };
struct Local {
    std::optional<EntityId> controller, pawn, observer_target, view_entity;
    std::optional<ViewMatrix> view_matrix;
    std::optional<Angles> view_angles;
    std::optional<CameraViewPunchState> view_punch;
    std::optional<EntityId> camera_recoil_pawn;
    std::optional<CameraRecoilPath> camera_recoil_path;
    // 人物眼部位置，不冒充第三人称或观战相机的位置。
    std::optional<Vec3> pawn_eye_position;
    // 原生相机的 FOV 覆盖值；零值表示没有覆盖，不能当成最终投影 FOV。
    std::optional<std::uint32_t> fov_override;
    std::optional<std::uint8_t> observer_mode;
    std::optional<MouseMapping> mouse_mapping;
    std::optional<bool> crosshair_recoil;
};
} // namespace cs2::model
