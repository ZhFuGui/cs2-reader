#pragma once
#include <cstddef>
#include <cstdint>

namespace cs2::game::input_profile {
inline constexpr std::uint32_t build = 14200;
inline constexpr std::size_t sensitivity = 0x2554c40, vertical_scale = 0x2554c50;
inline constexpr std::size_t yaw = 0x2554c60, pitch = 0x2554c70, zoom_ratio = 0x254d1d8;
inline constexpr std::size_t invert_y = 0x2554be0, cheats = 0x2551518, crosshair_recoil = 0x25ba6e0;
inline constexpr std::size_t cvar_data = 8, cvar_type = 0x28, cvar_flags = 0x30, cvar_value = 0x58;
inline constexpr std::size_t slot_manager = 0x278fa98, slot_count = 8;
inline constexpr std::size_t source_x = 0x2555590, source_y = 0x2554cb0;
inline constexpr std::size_t input_motion_function = 0xcdb810, input_scale_function = 0xb761c0;
} // namespace cs2::game::input_profile
