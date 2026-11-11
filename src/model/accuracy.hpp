#pragma once
#include "model/types.hpp"
namespace cs2::model {
enum class AccuracyStatus : unsigned {
    Unspecified, Valid, ReadFailed, SourceChanged, UnsupportedBuild, UnsupportedDispatch,
    DirtyVelocity, UnsupportedPhysics, EyeUnavailable, OwnerUnavailable, GroundUnavailable,
    RuleUnavailable, InvalidInput, NonfiniteResult, UnsupportedNormalization
};
enum class AccuracyBranch : unsigned { Unspecified, NoOwner, Forced, NoSpread, Normal };
struct AccuracyScalar {
    std::optional<float> value;
    AccuracyStatus status = AccuracyStatus::Unspecified;
};
struct WeaponAccuracyEvaluation {
    std::string model_id;
    std::optional<EntityId> owner;
    AccuracyScalar inaccuracy, spread;
    AccuracyBranch branch = AccuracyBranch::Unspecified;
    std::optional<float> movement, airborne, turning, lateral, penalty;
    std::optional<std::int32_t> mode_raw;
    std::optional<float> accuracy_state_time_seconds;
    std::uint64_t started_qpc = 0, read_finished_qpc = 0;
    std::uint32_t used_rules = 0; // 仅本批内部依赖位集，不传输地址或调试证明。
};
} // namespace cs2::model
