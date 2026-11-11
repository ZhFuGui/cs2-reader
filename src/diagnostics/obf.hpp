#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
// 编译期异或的字符串。字面量作为模板参数（NTTP）传入，不在 .rdata 里留下任何明文对象；
// 密文是 static constexpr 数组，解密经 volatile 读 + 不透明零掩码，避免被折叠回常量。
// 约束：只用于"必须留在二进制里的标识"（模块名、内核映像名、协议文案）；
// 能改成中性措辞的一律改掉，不要用本工具去藏"其实可以删掉"的字符串。
namespace cs2::diagnostics {
namespace detail {
constexpr std::uint8_t layer(std::size_t index) noexcept {
    return static_cast<std::uint8_t>(0x5Bu + static_cast<std::uint8_t>(index * 31u));
}
// 运行期恒为 0（没有任何写入点）；编译器无法据此证明解密结果是常量。
inline volatile std::uint8_t opaque_zero = 0;
template <std::size_t N> struct fixed_string {
    char value[N]{};
    constexpr fixed_string(const char (&text)[N]) noexcept {
        for (std::size_t i = 0; i < N; ++i)
            value[i] = text[i];
    }
};
template <std::size_t N> fixed_string(const char (&)[N]) -> fixed_string<N>;
template <std::size_t N> struct fixed_wstring {
    wchar_t value[N]{};
    constexpr fixed_wstring(const wchar_t (&text)[N]) noexcept {
        for (std::size_t i = 0; i < N; ++i)
            value[i] = text[i];
    }
};
template <std::size_t N> fixed_wstring(const wchar_t (&)[N]) -> fixed_wstring<N>;
template <fixed_string Text> struct narrow {
    static constexpr std::size_t size = sizeof(Text.value) / sizeof(char);
    static constexpr std::array<std::uint8_t, size> cipher = [] {
        std::array<std::uint8_t, size> out{};
        for (std::size_t i = 0; i < size; ++i)
            out[i] = static_cast<std::uint8_t>(static_cast<std::uint8_t>(Text.value[i]) ^ layer(i));
        return out;
    }();
    static std::string str() {
        const volatile std::uint8_t* source = cipher.data();
        std::string out(size - 1, '\0');
        for (std::size_t i = 0; i + 1 < size; ++i)
            out[i] = static_cast<char>(static_cast<std::uint8_t>(source[i] ^ layer(i) ^ opaque_zero));
        return out;
    }
};
template <fixed_wstring Text> struct wide {
    static constexpr std::size_t size = sizeof(Text.value) / sizeof(wchar_t);
    static constexpr std::array<std::uint16_t, size> cipher = [] {
        std::array<std::uint16_t, size> out{};
        for (std::size_t i = 0; i < size; ++i)
            out[i] = static_cast<std::uint16_t>(static_cast<std::uint16_t>(Text.value[i]) ^ layer(i));
        return out;
    }();
    static std::wstring wstr() {
        const volatile std::uint16_t* source = cipher.data();
        std::wstring out(size - 1, L'\0');
        for (std::size_t i = 0; i + 1 < size; ++i)
            out[i] = static_cast<wchar_t>(static_cast<std::uint16_t>(source[i] ^ layer(i) ^ opaque_zero));
        return out;
    }
    static std::u16string u16() {
        const volatile std::uint16_t* source = cipher.data();
        std::u16string out(size - 1, u'\0');
        for (std::size_t i = 0; i + 1 < size; ++i)
            out[i] = static_cast<char16_t>(static_cast<std::uint16_t>(source[i] ^ layer(i) ^ opaque_zero));
        return out;
    }
};
} // namespace detail
} // namespace cs2::diagnostics
#define OBF(literal) (::cs2::diagnostics::detail::narrow<literal>::str())
#define WOBF_W(literal) (::cs2::diagnostics::detail::wide<literal>::wstr())
#define WOBF_U16(literal) (::cs2::diagnostics::detail::wide<literal>::u16())
