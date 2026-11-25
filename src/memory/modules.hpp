#pragma once
#include "memory/read_channel.hpp"
#include <algorithm>
#include <readers/memory/process.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cs2::memory {
using Module = readers::memory::Module;
inline bool same_module_name(std::u16string_view left, std::u16string_view right) noexcept {
    const auto fold = [](char16_t c) { return c >= u'A' && c <= u'Z' ? c + 32 : c; };
    return std::ranges::equal(left, right, [fold](char16_t a, char16_t b) { return fold(a) == fold(b); });
}
} // namespace cs2::memory
