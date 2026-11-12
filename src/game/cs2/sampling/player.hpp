#pragma once
#include "game/cs2/sampling/common.hpp"
namespace cs2::game {
memory::Fields player_fields(memory::Address);
memory::Fields body_fields(memory::Address);
// 身份、Controller→Pawn 和骨骼关闭由类别批次统一完成。
model::Player project_player(const EntitySample& controller, const EntitySample* body);
struct PlayerSupplementRead {
    EntitySample controller;
    std::optional<EntitySample> body;
    model::Player& result;
};
void sample_player_supplements(memory::IReadChannel&, std::span<const PlayerSupplementRead>);
} // namespace cs2::game
