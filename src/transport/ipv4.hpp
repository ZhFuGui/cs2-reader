#pragma once
#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace cs2::transport {
// 数字 IPv4，返回主机序数值；不进行 DNS 查询，也不接受隐式缩写。
[[nodiscard]] inline std::optional<std::uint32_t> parse_ipv4(std::string_view text) noexcept {
    std::uint32_t address = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const auto separator = text.find('.');
        if ((i < 3) == (separator == std::string_view::npos))
            return {};
        const auto octet = text.substr(0, separator);
        if (octet.empty() || (octet.size() > 1 && octet.front() == '0'))
            return {};
        unsigned value = 0;
        const auto result = std::from_chars(octet.data(), octet.data() + octet.size(), value);
        if (result.ec != std::errc{} || result.ptr != octet.data() + octet.size() || value > 255)
            return {};
        address = (address << 8) | value;
        if (i < 3)
            text.remove_prefix(separator + 1);
    }
    return address;
}
} // namespace cs2::transport
