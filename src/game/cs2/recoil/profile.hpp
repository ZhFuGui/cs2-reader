#pragma once
#include <cstddef>
#include <cstdint>

// build14196离线模块核验；这些非反射布局不随output自动外推到其他版本。
namespace cs2::game::recoil_profile {
inline constexpr std::uint32_t build = 14196;
inline constexpr float seconds_per_tick = 1.0f / 64.0f;
inline constexpr std::size_t owner = 0x38;
inline constexpr std::size_t prediction_root = 0x223b370, world_time_root = 0x256a3f8;
inline constexpr std::size_t visual_context = 0x21c40a0, view_punch_decay = 0x2537640;
inline constexpr std::size_t current_seconds = 0x30, frame_seconds = 0x34;
inline constexpr std::size_t global_fraction = 0x38, predicted_fraction = 0x3c;
inline constexpr std::size_t interpolation_disabled = 0x40, global_tick = 0x44, simulation_fraction = 0x50;
inline constexpr std::size_t prediction_suppressed = 0x34, prediction_tick = 0x10c;
inline constexpr std::size_t world_offset = 0x30, world_clamp = 0x34, world_clamp_enabled = 0x38;
inline constexpr std::size_t world_tick_function = 0xba9270, world_seconds_function = 0xba9320;
inline constexpr std::size_t cvar_data = 8, cvar_value = 0x58;
inline constexpr std::size_t player_camera_function = 0x883dd0;
inline constexpr std::size_t observer_mode_function = 0x855030, observer_target_function = 0x855100;
inline constexpr std::size_t observer_dispatch_function = 0xa65770, observer_in_eye_function = 0x84fc40;
} // namespace cs2::game::recoil_profile
