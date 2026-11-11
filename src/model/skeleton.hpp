#pragma once
#include "model/types.hpp"

namespace cs2::model {
// 语义编号独立于模型原生索引，不允许将两个数值直接互换。
enum class BoneRole : std::uint8_t {
    Pelvis = 1,
    Spine0,
    Spine1,
    Spine2,
    Spine3,
    Neck,
    Head,
    LeftClavicle,
    LeftUpperArm,
    LeftForearm,
    LeftHand,
    RightClavicle,
    RightUpperArm,
    RightForearm,
    RightHand,
    LeftUpperLeg,
    LeftLowerLeg,
    LeftAnkle,
    RightUpperLeg,
    RightLowerLeg,
    RightAnkle
};
enum class SkeletonStatus : std::uint8_t { UnknownModel = 1, SourceUnavailable, VerifiedPartial, VerifiedComplete };
struct BoneWorldPose {
    Quaternion rotation;
    float uniform_scale;
};
struct BonePoint {
    std::uint16_t index;
    Vec3 position;
    std::optional<BoneRole> role;
    std::optional<BoneWorldPose> world_pose = std::nullopt;
};
struct BoneLink {
    std::uint16_t parent_index, child_index;
};
struct Skeleton {
    SkeletonStatus status = SkeletonStatus::SourceUnavailable;
    std::optional<std::string> model_name, profile_id;
    std::vector<BonePoint> points;
    std::vector<BoneLink> links;
    std::optional<bool> native_hitbox_set_available;
};
} // namespace cs2::model
