#pragma once
#include <array>
#include <cstddef>
#include <string_view>
// 由build/freeze_protocol.py生成；摘要来自随协议冻结的descriptor。
namespace cs2_reader::v1 {
inline constexpr std::array<std::byte,32> schema_sha256 = {
    std::byte{0xf1},std::byte{0x90},std::byte{0x58},std::byte{0x36},std::byte{0xae},std::byte{0xff},std::byte{0x62},std::byte{0x57},std::byte{0x45},std::byte{0x68},std::byte{0xe6},std::byte{0x53},std::byte{0x4f},std::byte{0x86},std::byte{0x6b},std::byte{0xa1},std::byte{0x4b},std::byte{0xfe},std::byte{0xe5},std::byte{0x16},std::byte{0xb0},std::byte{0xd7},std::byte{0x9a},std::byte{0x59},std::byte{0x5b},std::byte{0x57},std::byte{0xb1},std::byte{0x41},std::byte{0x2d},std::byte{0x74},std::byte{0x84},std::byte{0x46}
};
inline constexpr std::string_view schema_sha256_hex = "f1905836aeff62574568e6534f866ba14bfee516b0d79a595b57b1412d748446";
}
