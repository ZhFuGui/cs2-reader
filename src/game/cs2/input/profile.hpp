#pragma once
#include <cstddef>
#include <cstdint>

namespace cs2::game::input_profile {
inline constexpr std::uint32_t build = 14197;
inline constexpr std::size_t sensitivity = 0x2551030, vertical_scale = 0x2551040;
inline constexpr std::size_t yaw = 0x2551050, pitch = 0x2551060, zoom_ratio = 0x25492d8;
inline constexpr std::size_t invert_y = 0x2550fd0, cheats = 0x254d908, crosshair_recoil = 0x25b6bc0;
inline constexpr std::size_t cvar_data = 8, cvar_type = 0x28, cvar_flags = 0x30, cvar_value = 0x58;
inline constexpr std::size_t slot_manager = 0x278b4f8, slot_count = 8;
inline constexpr std::size_t source_x = 0x2551980, source_y = 0x25510a0;
inline constexpr std::size_t input_motion_function = 0xcdac20, input_scale_function = 0xb756b0;
} // namespace cs2::game::input_profile
