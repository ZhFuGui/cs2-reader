#pragma once
#include <cstddef>
#include <cstdint>

namespace cs2::game::world_profile {
// build14196 engine2.dll 原生地图查询与 IsInGame 分支；非反射布局不得跨版本外推。
inline constexpr std::uint32_t build = 14196;
inline constexpr std::size_t map_header = 0x210, map_header_bytes = 0x24;
inline constexpr std::size_t name_in_header = 0x8, sign_on_in_header = 0x20;
inline constexpr std::int32_t full_sign_on = 6;
inline constexpr std::size_t maximum_text_bytes = 1024;
} // namespace cs2::game::world_profile
