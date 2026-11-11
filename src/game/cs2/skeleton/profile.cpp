#include "game/cs2/skeleton/profile.hpp"
#include "game/cs2/skeleton/agents_14192.hpp"
#include "game/cs2/skeleton/hitboxes_14192.hpp"
#include <algorithm>

namespace cs2::game::skeleton {
namespace {
// 14196 已逐字节核对全部 80 份模型资源；资产布局 ID 保持不变，
// 当前游戏版本仍独立核验，不将未知版本套入已知骨架。
constexpr Profile verified_agents{profiles::agents_14192.id, 14196, profiles::agents_14192.bones};
}
const Profile* find_profile(std::string_view name, std::uint32_t build) noexcept {
    if (build != verified_agents.game_build)
        return nullptr;
    const auto& names = profiles::agent_models_14192;
    if (!std::binary_search(names.begin(), names.end(), name))
        return nullptr;
    return &verified_agents;
}
std::optional<std::uint64_t> hitbox_group_mask(std::string_view name, std::uint32_t build) noexcept {
    if (build != verified_agents.game_build)
        return std::nullopt;
    const auto& names = profiles::agent_models_14192;
    static_assert(profiles::agent_models_14192.size() == profiles::agent_hitbox_group_masks_14192.size());
    const auto found = std::lower_bound(names.begin(), names.end(), name);
    if (found == names.end() || *found != name)
        return std::nullopt;
    return profiles::agent_hitbox_group_masks_14192[static_cast<std::size_t>(found - names.begin())];
}
std::optional<std::string> canonical_model_name(std::string_view name) {
    if (name.empty())
        return {};
    std::string result;
    result.reserve(name.size());
    for (unsigned char c : name) {
        if (c < 32 || c > 126)
            return {}; // 不将不可验证的名字字节强行转换成别的资产。
        if (c == '\\')
            c = '/';
        if (c >= 'A' && c <= 'Z')
            c = static_cast<unsigned char>(c + ('a' - 'A'));
        result.push_back(static_cast<char>(c));
    }
    if (result.ends_with(".vmdl_c"))
        result.resize(result.size() - 2);
    return result;
}
} // namespace cs2::game::skeleton
