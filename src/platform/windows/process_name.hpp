#pragma once
#include <cstdint>
#include <string_view>

namespace cs2::platform::windows {
struct ProcessNameKey {
    std::uint64_t first, second;
    std::size_t length;
    bool operator==(const ProcessNameKey&) const = default;
};
// 这是不可逆的名称标识，并非非对称加密；两个独立通道与长度一起匹配。
constexpr ProcessNameKey hash_process_name(std::wstring_view name) noexcept {
    ProcessNameKey key{14695981039346656037ULL, 7809847782465536322ULL, name.size()};
    for (auto character : name) {
        const auto folded =
            static_cast<std::uint16_t>(character >= L'A' && character <= L'Z' ? character + 32 : character);
        for (unsigned shift : {0U, 8U}) {
            const auto value = static_cast<std::uint8_t>(folded >> shift);
            key.first = (key.first ^ value) * 1099511628211ULL;
            key.second = (key.second ^ value) * 14029467366897019727ULL;
        }
    }
    return key;
}
template <std::size_t N> consteval ProcessNameKey process_name(const wchar_t (&name)[N]) noexcept {
    return hash_process_name(std::wstring_view(name, N - 1));
}
} // namespace cs2::platform::windows
