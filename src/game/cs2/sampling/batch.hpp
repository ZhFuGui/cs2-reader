#pragma once
#include "capture/clock.hpp"
#include "game/cs2/entity/directory.hpp"
#include <span>
#include <variant>

namespace cs2::game {
class AccuracyRules;
using EntityValue =
    std::variant<std::monostate, model::Player, model::Weapon, model::Bomb, model::Projectile, model::EntityState>;
// 调用方拥有结果槽位；组大小只决定合批粒度，不限制目录覆盖。
// 每组独占通道和暂存，所有来源关闭完成后才交还结果。
void sample_entities(memory::IReadChannel&, const Context&, const Directory&, std::span<const EntityRef* const>,
                     std::span<EntityValue>, bool bones, const capture::IClock&, const AccuracyRules*);
} // namespace cs2::game
