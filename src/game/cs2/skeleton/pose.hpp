#pragma once
#include "game/cs2/entity/layout.hpp"
#include "model/skeleton.hpp"
#include <cstddef>

namespace cs2::game::skeleton {
// build14192原生已求值CTransform：Getter 0xab1ff0、Hitbox路径0xab19c0和组合器0x174a6e0核验。
// 同一记录内的位置、累计世界缩放与xyzw旋转共同定义骨局部到世界变换。
struct NativeBoneTransform {
    model::Vec3 position;
    float uniform_scale;
    model::Quaternion rotation;
};
static_assert(sizeof(NativeBoneTransform) == layout::bone_stride);
static_assert(offsetof(NativeBoneTransform, uniform_scale) == 12);
static_assert(offsetof(NativeBoneTransform, rotation) == 16);
inline std::optional<model::BoneWorldPose> world_pose(const NativeBoneTransform& value) noexcept {
    if (!model::finite(value.position) || !std::isfinite(value.uniform_scale) ||
        !model::unit_quaternion(value.rotation))
        return std::nullopt;
    // 保留原生值；零和负缩放均是有限数值，不暗中替换为1或再乘实体缩放。
    return model::BoneWorldPose{value.rotation, value.uniform_scale};
}
} // namespace cs2::game::skeleton
