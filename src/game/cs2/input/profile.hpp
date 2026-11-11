#pragma once
#include <cstddef>
#include <cstdint>

namespace cs2::game::input_profile {
inline constexpr std::uint32_t build = 14196;
inline constexpr std::size_t sensitivity = 0x2567c40, vertical_scale = 0x2567c50;
inline constexpr std::size_t yaw = 0x2567c60, pitch = 0x2567c70, zoom_ratio = 0x25600d8;
inline constexpr std::size_t invert_y = 0x2567be0, cheats = 0x2564538, crosshair_recoil = 0x25cd798;
inline constexpr std::size_t cvar_data = 8, cvar_type = 0x28, cvar_flags = 0x30, cvar_value = 0x58;
inline constexpr std::size_t slot_manager = 0x27a2008, slot_count = 8;
inline constexpr std::size_t source_x = 0x2568590, source_y = 0x2567cb0;
inline constexpr std::size_t input_motion_function = 0xce5e80, input_scale_function = 0xb81ba0;
} // namespace cs2::game::input_profile
