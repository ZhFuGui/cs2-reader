#pragma once
#include "game/cs2/entity/directory.hpp"
#include "model/eye.hpp"
#include "model/recoil.hpp"

namespace cs2::game {
struct DirectionComponents {
    std::optional<model::EyeAngleBasis> eye_basis;
    std::optional<model::AimPunchState> aim_punch;
};
// 同批当前读取；上下文是查询分支，不是实体或会话身份。
bool has_shared_context(const DirectionComponents&) noexcept;
// 非委托source2的角度不依赖选择器；其它来源仍要求相同上下文。
bool has_compatible_context(const DirectionComponents&) noexcept;
DirectionComponents sample_direction(memory::IReadChannel&, const Context&, const Directory&, const EntityRef&);
} // namespace cs2::game
