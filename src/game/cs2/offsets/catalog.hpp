#pragma once
#include "output/client_dll.hpp"
#include "output/offsets.hpp"
#include <cstdint>
#include <string_view>
namespace cs2::game::offsets {
namespace client = cs2_dumper::offsets::client_dll;
namespace engine = cs2_dumper::offsets::engine2_dll;
namespace schema = cs2_dumper::schemas::client_dll;
std::uint32_t expected_build() noexcept;
std::string_view generated_at() noexcept;
} // namespace cs2::game::offsets
