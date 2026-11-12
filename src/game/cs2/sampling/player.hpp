#pragma once
#include "game/cs2/sampling/common.hpp"
namespace cs2::game {
memory::Fields player_fields(memory::Address);
memory::Fields body_fields(memory::Address);
// 身份、Controller→Pawn 和骨骼关闭由类别批次统一完成。
model::Player sample_player(memory::IReadChannel&, const Context&, const Directory&, const EntitySample& controller,
                            const EntitySample* body);
} // namespace cs2::game
