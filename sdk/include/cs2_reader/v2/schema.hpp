#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
// 由build/freeze_protocol.py生成；摘要来自随协议冻结的descriptor。
namespace cs2_reader::v2 {
inline constexpr std::uint32_t protocol_version = 2;
inline constexpr std::array<std::byte,32> schema_sha256 = {
    std::byte{0xde},std::byte{0x58},std::byte{0x9b},std::byte{0x63},std::byte{0xc5},std::byte{0xef},std::byte{0xe2},std::byte{0x64},std::byte{0x92},std::byte{0xa2},std::byte{0x67},std::byte{0xba},std::byte{0xfe},std::byte{0xd7},std::byte{0x09},std::byte{0x03},std::byte{0x4f},std::byte{0x8a},std::byte{0xd3},std::byte{0x1c},std::byte{0xec},std::byte{0x02},std::byte{0x99},std::byte{0x0a},std::byte{0x36},std::byte{0x62},std::byte{0xa8},std::byte{0x7e},std::byte{0xac},std::byte{0xaf},std::byte{0xf7},std::byte{0x27}
};
inline constexpr std::string_view schema_sha256_hex = "de589b63c5efe26492a267bafed709034f8ad31cec02990a3662a87eacaff727";
}
