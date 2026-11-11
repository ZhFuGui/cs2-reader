#include "game/cs2/offsets/catalog.hpp"
#include "offset_metadata.hpp"
namespace cs2::game::offsets {
std::uint32_t expected_build() noexcept {
    return generated::build_number;
}
std::string_view generated_at() noexcept {
    return generated::timestamp;
}
} // namespace cs2::game::offsets
