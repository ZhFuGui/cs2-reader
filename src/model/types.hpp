#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cs2::model {
struct Vec3 {
    float x, y, z;
};
struct Angles {
    float pitch, yaw, roll;
};
struct Quaternion {
    float x, y, z, w;
};
// 使用范数平方容差，避免把坏值归一化成看似有效的旋转。
inline constexpr double quaternion_norm_squared_tolerance = .001;
inline bool unit_quaternion(Quaternion q) noexcept {
    if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w))
        return false;
    const auto x = static_cast<double>(q.x), y = static_cast<double>(q.y);
    const auto z = static_cast<double>(q.z), w = static_cast<double>(q.w);
    return std::abs((x * x + y * y + z * z + w * w) - 1.) <= quaternion_norm_squared_tolerance;
}
using ViewMatrix = std::array<float, 16>;
inline bool finite(Vec3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline bool finite(Angles v) noexcept {
    return std::isfinite(v.pitch) && std::isfinite(v.yaw) && std::isfinite(v.roll);
}
struct EntityId {
    std::uint32_t handle = 0;
    bool operator==(const EntityId&) const = default;
};
struct Interval {
    std::uint64_t started_qpc = 0, read_finished_qpc = 0, finished_qpc = 0;
};
struct Transform {
    std::optional<Vec3> position;
    std::optional<Angles> rotation;
    std::optional<float> scale;
    std::optional<bool> dormant;
};
struct EntityState {
    EntityId id;
    std::string designer_name;
    Transform transform;
    std::optional<Vec3> world_velocity;
    std::optional<std::uint8_t> team, life_state;
    std::optional<std::int32_t> health, max_health;
    std::optional<std::uint32_t> flags;
    std::optional<EntityId> owner;
};
} // namespace cs2::model
