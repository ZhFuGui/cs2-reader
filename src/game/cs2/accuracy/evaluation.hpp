#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace cs2::accuracy {
// 求值器只接受已解析的当前输入；对象身份、来源、批次和缓存有效性由适配器核验。
// 空值表示没有取得/核验，绝不自动解释为 0、false、无所有者或在地面。
struct Velocity { float x, y, z; }; // 原生绝对速度，游戏世界单位/秒。
struct EyeAngles { float pitch, yaw; }; // 原生 EyeAngles getter 的角度（度）；本公式不使用 roll。
enum class Ownership { Unknown, Absent, Present };
enum class MaximumSpeedPath { Unknown, WeaponData, PlantingC4 };
enum class Branch { Unavailable, NoOwner, Forced, NoSpread, Normal };
enum class Error { None, MissingInput, InvalidInput, UnsupportedPath, NonFiniteResult };

struct WeaponState {
    std::optional<float> accuracy_penalty; // C_CSWeaponBase::m_fAccuracyPenalty，已由游戏更新。
    std::optional<float> turning_inaccuracy; // m_flTurningInaccuracy，已由游戏更新。
    std::optional<std::int32_t> mode; // m_weaponMode；实际原生值在 [0,2) 外时 getter 选择数组第 0 项。
    MaximumSpeedPath maximum_speed_path = MaximumSpeedPath::Unknown;
    std::optional<bool> started_arming; // 仅 C_C4 特有最大速度 getter 需要。
};
struct WeaponData {
    std::optional<std::array<float,2>> maximum_speed; // m_flMaxSpeed，世界单位/秒。
    std::optional<std::array<float,2>> movement; // m_flInaccuracyMove，两个 firing-mode 分量。
    std::optional<float> jump_initial, jump_apex; // m_flInaccuracyJumpInitial / Apex。
    std::optional<std::array<float,2>> spread; // m_flSpread；独立于完整 Inaccuracy。
};
struct OwnerState {
    std::optional<Velocity> velocity; // 仅允许来源等价于原生 GetAbsVelocity 的结果。
    std::optional<bool> walking; // m_bIsWalking，决定移动项是否取四次方根。
    std::optional<bool> ground_entity_present; // 完整 ground handle 解析结果，不用 flags 猜测。
    std::optional<EyeAngles> native_eye_angles; // 仅开启横向附加项且达到速度阈值时必需。
};
struct Rules {
    std::optional<float> force; // weapon_accuracy_forcespread，优先于 nospread。
    std::optional<bool> no_spread; // weapon_accuracy_nospread。
    std::optional<float> air_scale, jump_impulse; // weapon_air_spread_scale / sv_jump_impulse。
    std::optional<bool> lateral_enabled; // sv_strafing_inaccuracy_enabled。
    std::optional<float> lateral_bias, lateral_scale; // sv_strafing_inaccuracy_bias / scale。
};
struct Inputs {
    Ownership ownership = Ownership::Unknown;
    WeaponState weapon;
    WeaponData data;
    OwnerState owner;
    Rules rules;
};
struct InaccuracyResult {
    std::optional<float> value;
    // 原生两个可选输出只在普通路径写入；提前返回时保持缺失，不填零。
    std::optional<float> movement, airborne;
    std::optional<float> turning, lateral;
    Branch branch = Branch::Unavailable;
    Error error = Error::None;
    std::string_view dependency;
};
struct ScalarResult {
    std::optional<float> value;
    Error error = Error::None;
    std::string_view dependency;
};
InaccuracyResult evaluate_inaccuracy(const Inputs&) noexcept;
ScalarResult evaluate_spread(const WeaponState&,const WeaponData&) noexcept;
} // namespace cs2::accuracy
