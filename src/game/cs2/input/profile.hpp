#pragma once
#include <cstddef>
#include <cstdint>

namespace cs2::game::input_profile {
inline constexpr std::uint32_t build = 14201;
inline constexpr std::size_t sensitivity = 0x255bc40, vertical_scale = 0x255bc50;
inline constexpr std::size_t yaw = 0x255bc60, pitch = 0x255bc70, zoom_ratio = 0x25541d8;
inline constexpr std::size_t invert_y = 0x255bbe0, cheats = 0x2558518, crosshair_recoil = 0x25c16e0;
inline constexpr std::size_t cvar_data = 8, cvar_type = 0x28, cvar_flags = 0x30, cvar_value = 0x58;
inline constexpr std::size_t slot_manager = 0x2796a98, slot_count = 8;
inline constexpr std::size_t source_x = 0x255c590, source_y = 0x255bcb0;
inline constexpr std::size_t input_motion_function = 0xce06a0, input_scale_function = 0xb7aca0;
} // namespace cs2::game::input_profile
