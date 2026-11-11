#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

// build14192的离线核验计算工具；只计算角度，不读取进程、不生成鼠标输入。
namespace cs2_reader::v1::recoil {
inline constexpr std::uint32_t supported_build = 14192;
inline constexpr float seconds_per_tick = 1.0f / 64.0f;
inline constexpr float camera_aim_fraction = .45f;
struct Angle {
    float pitch, yaw, roll;
};
struct Time {
    std::int32_t tick;
    float fraction;
};
namespace detail {
inline Angle add(Angle a, Angle b) {
    return {a.pitch + b.pitch, a.yaw + b.yaw, a.roll + b.roll};
}
inline Angle mul(Angle a, float factor) {
    return {a.pitch * factor, a.yaw * factor, a.roll * factor};
}
inline bool finite(Angle a) {
    return std::isfinite(a.pitch) && std::isfinite(a.yaw) && std::isfinite(a.roll);
}
inline float length(Angle a) {
    return std::sqrt(a.pitch * a.pitch + a.yaw * a.yaw + a.roll * a.roll);
}
struct Quaternion {
    float x, y, z, w;
};
inline Quaternion quaternion(Angle a) {
    constexpr float factor = .0087266462599716479f;
    const float sp = std::sin(a.pitch * factor), cp = std::cos(a.pitch * factor);
    const float sy = std::sin(a.yaw * factor), cy = std::cos(a.yaw * factor);
    const float sr = std::sin(a.roll * factor), cr = std::cos(a.roll * factor);
    return {sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy,
            cr * cp * cy + sr * sp * sy};
}
inline Angle angles(Quaternion q) {
    constexpr float degrees = 57.29577951308232f;
    const float fx = 1 - 2 * q.y * q.y - 2 * q.z * q.z, fy = 2 * q.x * q.y + 2 * q.w * q.z,
                fz = 2 * q.x * q.z - 2 * q.w * q.y;
    const float lz = 2 * q.y * q.z + 2 * q.w * q.x, uz = 1 - 2 * q.x * q.x - 2 * q.y * q.y;
    const float horizontal = std::sqrt(fx * fx + fy * fy);
    return {std::atan2(-fz, horizontal) * degrees, std::atan2(fy, fx) * degrees, std::atan2(lz, uz) * degrees};
}
inline Angle interpolate(Angle a, Angle b, float t) {
    const auto clamp = [](Angle v) {
        return Angle{std::clamp(v.pitch, -89.f, 89.f), std::clamp(v.yaw, -89.f, 89.f), std::clamp(v.roll, -89.f, 89.f)};
    };
    const auto p = quaternion(clamp(a)), q = quaternion(clamp(b));
    const float dot = ((p.x * q.x + p.y * q.y) + p.z * q.z) + p.w * q.w, d = std::abs(dot);
    const float centered = t - .5f;
    const float first = (.21563799679279327f * d - 1.0602099895477295f) * d + .8480129837989807f;
    const float second =
        ((3.5564498901367188f - 1.435189962387085f * d) * d - 3.2451999187469482f) * d + 1.090399980545044f;
    float weight = ((second * centered * centered + first) * t * centered * (t - 1)) + t;
    const float other = 1 - weight;
    if (dot <= 0)
        weight = -weight;
    Quaternion result{p.x * other + q.x * weight, p.y * other + q.y * weight, p.z * other + q.z * weight,
                      p.w * other + q.w * weight};
    const float norm =
        std::sqrt(((result.x * result.x + result.y * result.y) + result.z * result.z) + result.w * result.w);
    if (norm == 0)
        return {};
    result = {result.x / norm, result.y / norm, result.z / norm, result.w / norm};
    return angles(result);
}
} // namespace detail

// 此函数仅在“没有新的开火/受击冲量、换枪或世界切换”这一状态假设下按给定时间求值。
// elapsed_seconds由同一原生游戏时域计算，不能直接用QPC差、网络延迟或帧年龄替代。
inline std::optional<Angle> evaluate_component(Angle base, Angle velocity, float elapsed_seconds,
                                               bool scale_twice = true) {
    using namespace detail;
    if (!finite(base) || !finite(velocity) || !std::isfinite(elapsed_seconds))
        return {};
    const float elapsed = std::max(0.f, elapsed_seconds);
    const float step_position = elapsed * 128.f;
    if (!std::isfinite(step_position) || static_cast<double>(step_position) > INT32_MAX - 2)
        return {};
    const int index = static_cast<int>(step_position);
    const int generated = std::min(index + 1, 128);
    std::array<Angle, 129> samples{};
    samples[0] = base;
    constexpr float step = 1.f / 128.f, half_step = 1.f / 256.f;
    const float angle_decay = std::exp(-.0625f);
    for (int i = 1; i <= generated; ++i) {
        Angle a = mul(samples[i - 1], angle_decay);
        const float magnitude = length(a);
        a = magnitude <= .140625f ? Angle{} : mul(a, 1.f - .140625f / magnitude);
        Angle before = mul(velocity, std::exp(-4.5f * (static_cast<float>(i - 1) * step)));
        Angle after = mul(velocity, std::exp(-4.5f * (static_cast<float>(i) * step)));
        if (length(before) < .03125f)
            before = {};
        if (length(after) < .03125f)
            after = {};
        samples[i] = add(add(a, mul(before, half_step)), mul(after, half_step));
    }
    const auto at = [&](int i) {
        return i <= generated ? samples[i]
                              : mul(samples[generated], std::pow(angle_decay, static_cast<float>(i - generated)));
    };
    auto result = interpolate(at(index), at(index + 1), step_position - static_cast<float>(index));
    if (length(result) < .03125f)
        result = {};
    if (scale_twice)
        result = mul(result, 2.f);
    return finite(result) ? std::optional(result) : std::nullopt;
}
inline std::optional<float> elapsed(Time target, Time base) {
    if (!std::isfinite(target.fraction) || !std::isfinite(base.fraction))
        return {};
    const auto tick = static_cast<std::int64_t>(target.tick) - base.tick;
    if (tick < INT32_MIN || tick > INT32_MAX)
        return {};
    return (static_cast<float>(tick) + (target.fraction - base.fraction)) * seconds_per_tick;
}
inline std::optional<Angle> evaluate_component(Angle base, Angle velocity, Time target, Time origin,
                                               bool scale_twice = true) {
    const auto delta = elapsed(target, origin);
    return delta ? evaluate_component(base, velocity, *delta, scale_twice) : std::nullopt;
}
inline std::optional<Angle> evaluate_view_punch(Angle base, Time origin, float evaluation_seconds,
                                                float decay_exponent) {
    using namespace detail;
    if (!finite(base) || !std::isfinite(origin.fraction) || !std::isfinite(evaluation_seconds) ||
        !std::isfinite(decay_exponent))
        return {};
    const float origin_seconds =
        static_cast<float>(origin.tick) * seconds_per_tick + origin.fraction * seconds_per_tick;
    const float exponent = (evaluation_seconds - origin_seconds) * decay_exponent;
    return exponent > 0 ? mul(base, std::exp(-exponent)) : base;
}
} // namespace cs2_reader::v1::recoil
