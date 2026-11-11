#pragma once
#include "model/types.hpp"
#include <algorithm>
#include <span>

namespace cs2::game::eye {
struct Quaternion {
    float x, y, z, w;
};
inline bool finite(Quaternion q) {
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}
inline Quaternion quaternion(model::Angles a) {
    constexpr float half_radians = .008726646192371845f;
    const float sp = std::sin(a.pitch * half_radians), cp = std::cos(a.pitch * half_radians);
    const float sy = std::sin(a.yaw * half_radians), cy = std::cos(a.yaw * half_radians);
    const float sr = std::sin(a.roll * half_radians), cr = std::cos(a.roll * half_radians);
    return {cy * sr * cp - sy * cr * sp, cy * cr * sp + sy * sr * cp, sy * cr * cp - cy * sr * sp,
            cy * cr * cp + sy * sr * sp};
}
inline Quaternion normalized(Quaternion q) {
    const float norm = std::sqrt((q.x * q.x + q.y * q.y) + (q.z * q.z + q.w * q.w));
    return norm == 0 ? Quaternion{0, 0, 0, 1} : Quaternion{q.x / norm, q.y / norm, q.z / norm, q.w / norm};
}
inline Quaternion compose(Quaternion a, Quaternion b) {
    return normalized(
        {((a.w * b.x + a.x * b.w) + a.y * b.z) - a.z * b.y, ((a.w * b.y - a.x * b.z) + a.y * b.w) + a.z * b.x,
         ((a.w * b.z + a.x * b.y) - a.y * b.x) + a.z * b.w, ((a.w * b.w - a.x * b.x) - a.y * b.y) - a.z * b.z});
}
inline model::Angles angles(Quaternion q) {
    constexpr float degrees = 57.2957763671875f;
    const float xx = q.x * (q.x + q.x), yy = q.y * (q.y + q.y), zz = q.z * (q.z + q.z);
    const float fx = (1 - yy) - zz, fy = (q.y + q.y) * q.x + (q.z + q.z) * q.w;
    const float fz = (q.z + q.z) * q.x - (q.y + q.y) * q.w;
    const float horizontal = std::sqrt(fy * fy + fx * fx);
    const float pitch = std::atan2(-fz, horizontal) * degrees;
    if (horizontal <= .001f) {
        const float left_x = (q.y + q.y) * q.x - (q.z + q.z) * q.w;
        const float left_y = (1 - xx) - zz;
        return {pitch, std::atan2(-left_x, left_y) * degrees, 0};
    }
    return {pitch, std::atan2(fy, fx) * degrees,
            std::atan2((q.x + q.x) * q.w + (q.z + q.z) * q.y, (1 - xx) - yy) * degrees};
}
// 原生QAngle特化：无AimPunch的±89度裁剪；跨±180度走四元数的短弧。
inline model::Angles interpolate(model::Angles a, model::Angles b, float t, bool linear = false,
                                 bool direct_endpoints = true) {
    if (direct_endpoints && t == 0)
        return a;
    if ((direct_endpoints && t == 1) || (a.pitch == b.pitch && a.yaw == b.yaw && a.roll == b.roll))
        return b;
    if (linear)
        return {b.pitch * t + a.pitch * (1 - t), b.yaw * t + a.yaw * (1 - t), b.roll * t + a.roll * (1 - t)};
    const auto p = quaternion(a), q = quaternion(b);
    const float dot = (p.x * q.x + p.y * q.y) + (p.z * q.z + p.w * q.w), d = std::abs(dot);
    const float centered = t - .5f;
    const float first = (.21563799679279327f * d - 1.0602099895477295f) * d + .8480129837989807f;
    const float second =
        ((3.5564498901367188f - 1.435189962387085f * d) * d - 3.2451999187469482f) * d + 1.090399980545044f;
    float weight = (second * centered * centered + first) * (centered * t * (t - 1)) + t;
    const float other = 1 - weight;
    if (dot <= 0)
        weight = -weight;
    return angles(normalized({q.x * weight + p.x * other, q.y * weight + p.y * other, q.z * weight + p.z * other,
                              q.w * weight + p.w * other}));
}
struct HistorySample {
    std::int32_t tick;
    model::Angles value;
};
struct HistoryPolicy {
    float query, delay, extrapolation_limit;
    std::int32_t minimum_extrapolation_tick;
    bool no_delay, extrapolate, linear;
};
inline std::optional<model::Angles> evaluate_history(std::span<const HistorySample> newest_first, HistoryPolicy policy,
                                                     model::Angles fallback) {
    if (!std::isfinite(policy.query) || !std::isfinite(policy.delay) || !std::isfinite(policy.extrapolation_limit))
        return {};
    for (const auto& sample : newest_first)
        if (!model::finite(sample.value))
            return {};
    if (newest_first.empty())
        return model::finite(fallback) ? std::optional(fallback) : std::nullopt;
    if (newest_first.size() == 1)
        return newest_first.front().value;
    const float target = policy.no_delay ? policy.query : policy.query - policy.delay;
    for (std::size_t i = 0; i + 1 < newest_first.size(); ++i) {
        const auto& newer = newest_first[i];
        const auto& older = newest_first[i + 1];
        if (!newer.tick || !older.tick)
            break;
        const float newer_time = static_cast<float>(newer.tick) / 64.f;
        const float older_time = static_cast<float>(older.tick) / 64.f;
        if (older_time > target)
            continue;
        if (newer_time < target) {
            if (policy.extrapolate && older.tick && policy.delay > .000001f &&
                older.tick >= policy.minimum_extrapolation_tick) {
                const float future = policy.query - policy.delay;
                if (newer.tick != older.tick && future > newer_time) {
                    const float duration =
                        static_cast<float>(static_cast<std::int64_t>(newer.tick) - older.tick) / 64.f;
                    const float t = std::min(future - newer_time, policy.extrapolation_limit) / duration + 1;
                    const auto v = interpolate(older.value, newer.value, t, false, false);
                    return model::finite(v) ? std::optional(v) : std::nullopt;
                }
            }
            return newer.value;
        }
        if (newer.tick <= older.tick)
            return older.value;
        const float t = std::clamp((target - older_time) / (newer_time - older_time), 0.f, 2.f);
        // build14192的0x8ce500以head+i+1再次选择第三候选，与older同一条记录；
        // 在本批稳定副本上older.tick-third.tick恒为零，不进入三点特化。
        const auto v = interpolate(older.value, newer.value, t, policy.linear);
        return model::finite(v) ? std::optional(v) : std::nullopt;
    }
    return newest_first.back().value;
}
} // namespace cs2::game::eye
