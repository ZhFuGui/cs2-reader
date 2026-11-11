#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
namespace cs2::transport {
struct Record {
    std::vector<std::byte> bytes;
    std::vector<std::size_t> stamp_offsets;
    std::optional<std::uint64_t> source_token;
};
} // namespace cs2::transport
