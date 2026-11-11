#pragma once
#include <cstdint>
#include <optional>

namespace cs2::model {
enum class NativeInputSource : std::uint32_t { Mouse = 1, Analog = 2 };
struct MouseMapping {
    std::optional<NativeInputSource> source;
    std::optional<float> sensitivity, vertical_scale, yaw, pitch, zoom_ratio;
    std::optional<bool> inverted_y;
    std::optional<float> effective_pitch, pawn_fov_adjust, pawn_mouse_sensitivity;
    // 每个原生累计鼠标增量对应的角度导数，不是桌面像素或鼠标设备校准。
    std::optional<float> yaw_degrees_per_delta, pitch_degrees_per_delta;
};
} // namespace cs2::model
