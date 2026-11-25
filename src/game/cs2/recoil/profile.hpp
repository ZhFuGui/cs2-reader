#pragma once
#include <cstddef>
#include <cstdint>

// build14200离线模块核验；这些非反射布局不随output自动外推到其他版本。
namespace cs2::game::recoil_profile {
inline constexpr std::uint32_t build = 14200;
inline constexpr float seconds_per_tick = 1.0f / 64.0f;
inline constexpr std::size_t owner = 0x38;
inline constexpr std::size_t prediction_root = 0x2229720, world_time_root = 0x25573f8;
inline constexpr std::size_t visual_context = 0x21b3088, view_punch_decay = 0x25246a0;
inline constexpr std::size_t current_seconds = 0x30, frame_seconds = 0x34;
inline constexpr std::size_t global_fraction = 0x38, predicted_fraction = 0x3c;
inline constexpr std::size_t interpolation_disabled = 0x40, global_tick = 0x44, simulation_fraction = 0x50;
inline constexpr std::size_t prediction_suppressed = 0x34, prediction_tick = 0x10c;
inline constexpr std::size_t world_offset = 0x30, world_clamp = 0x34, world_clamp_enabled = 0x38;
inline constexpr std::size_t world_tick_function = 0xb9d8a0, world_seconds_function = 0xb9d950;
inline constexpr std::size_t cvar_data = 8, cvar_value = 0x58;
inline constexpr std::size_t player_camera_function = 0x87e840;
inline constexpr std::size_t observer_mode_function = 0x84faa0, observer_target_function = 0x84fb70;
inline constexpr std::size_t observer_dispatch_function = 0xa5f270, observer_in_eye_function = 0x84a6b0;
} // namespace cs2::game::recoil_profile
