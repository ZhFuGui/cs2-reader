#pragma once
#include "game/cs2/offsets/catalog.hpp"
#include <cstddef>
#include <cstdint>
namespace cs2::game::eye_profile {
inline constexpr std::uint32_t build = 14201;
inline constexpr std::size_t getter = 0xc70860, input_getter = 0x951ce0, camera_view_getter = 0x854090;
inline constexpr std::size_t context = 0x21ba088, lane_mode = 0x21ba08c, time = 0x21ba090;
inline constexpr std::size_t gate_a = 0x2536520, gate_b = 0x255a0f5, gate_c_data = 0x255b370, gate_slot = 0x21faf30;
inline constexpr std::size_t prediction_flag = 0x14c5;
// m_angEyeAngles 是带插值状态的原生存储；其整体位置随 schema 更新，内部布局经本版本 getter 核验。
inline constexpr std::size_t angle_storage = offsets::schema::C_CSPlayerPawn::m_angEyeAngles;
inline constexpr std::size_t interpolation = angle_storage + 0x10, cache_value = angle_storage + 0x18;
inline constexpr std::size_t cache_time = angle_storage + 0x30;
inline constexpr std::size_t flags = angle_storage + 0x48, dirty = angle_storage + 0x49, active = angle_storage + 0x70;
inline constexpr std::size_t prediction_getter = 0x93c9c0;
inline constexpr std::size_t world_rotation_getter = 0x15f660;
inline constexpr std::size_t history = angle_storage + 0x38, history_vtable = 0x1c3b080;
inline constexpr std::size_t history_fallback = 0xa72010, history_type = 0xa67d50;
inline constexpr std::size_t history_delay = 0xa68110, history_after_fallback = 0xa68100;
inline constexpr std::size_t no_history_delay = 0x21ba098, three_sample_enabled = 0x2254d78;
inline constexpr std::size_t extrapolation_enabled = 0x2368870, extrapolation_min_tick = 0x2368874;
inline constexpr std::size_t extrapolation_limit = 0x2254d7c, native_entity_table = 0x2368860;
inline constexpr std::size_t scene_transform_dirty = 0x110;
} // namespace cs2::game::eye_profile
