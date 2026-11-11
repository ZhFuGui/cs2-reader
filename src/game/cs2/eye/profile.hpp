#pragma once
#include <cstddef>
#include <cstdint>
namespace cs2::game::eye_profile {
inline constexpr std::uint32_t build = 14196;
inline constexpr std::size_t getter = 0xc76d40, input_getter = 0x953310, camera_view_getter = 0x8556f0;
inline constexpr std::size_t context = 0x21c40a0, lane_mode = 0x21c40a4, time = 0x21c40a8;
inline constexpr std::size_t gate_a = 0x2542c04, gate_b = 0x25655e0, gate_c_data = 0x2567370, gate_slot = 0x2205030;
inline constexpr std::size_t prediction_flag = 0x14c5;
inline constexpr std::size_t interpolation = 0x3610, cache_value = 0x3618, cache_time = 0x3630;
inline constexpr std::size_t flags = 0x3648, dirty = 0x3649, active = 0x3670;
inline constexpr std::size_t prediction_getter = 0x93ddc0;
inline constexpr std::size_t world_rotation_getter = 0x15fbc0;
inline constexpr std::size_t history = 0x3638, history_vtable = 0x1c3fa38;
inline constexpr std::size_t history_fallback = 0xa73c20, history_type = 0xa699c0;
inline constexpr std::size_t history_delay = 0xa69d80, history_after_fallback = 0xa69d70;
inline constexpr std::size_t no_history_delay = 0x21c40b0, three_sample_enabled = 0x225de40;
inline constexpr std::size_t extrapolation_enabled = 0x2374c00, extrapolation_min_tick = 0x2374c04;
inline constexpr std::size_t extrapolation_limit = 0x225de44, native_entity_table = 0x2374bf0;
inline constexpr std::size_t scene_transform_dirty = 0x110;
} // namespace cs2::game::eye_profile
