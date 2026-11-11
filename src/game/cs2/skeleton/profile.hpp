#pragma once
#include "model/skeleton.hpp"
#include <span>
#include <string_view>

namespace cs2::game::skeleton {
struct BoneDefinition {
    std::uint16_t index, parent_index;
    model::BoneRole role;
};
struct Profile {
    std::string_view id;
    std::uint32_t game_build;
    std::span<const BoneDefinition> bones;
};
// 仅精确匹配已核验当前build的模型白名单；不按文件夹或名字片段猜测。
const Profile* find_profile(std::string_view canonical_model_name, std::uint32_t game_build) noexcept;
std::optional<std::uint64_t> hitbox_group_mask(std::string_view canonical_model_name, std::uint32_t game_build) noexcept;
std::optional<std::string> canonical_model_name(std::string_view);
} // namespace cs2::game::skeleton
