#pragma once
#include "model/types.hpp"

namespace cs2::model {
struct GameTimePoint {
    std::int32_t tick;
    float fraction;
};
enum class VisualRecoilTimePolicy : std::uint8_t { SeparateRenderTime = 1, SharedSimulationTime };
struct RecoilTimes {
    std::uint32_t world_group;
    VisualRecoilTimePolicy visual_policy;
    std::optional<GameTimePoint> simulation, visual_predictable, visual_unpredictable;
    std::optional<std::int32_t> native_context;
};
struct PredictableAimPunch {
    GameTimePoint base_time;
    Angles base_angle, base_angular_velocity;
};
struct UnpredictableAimPunch {
    std::int32_t base_tick;
    Angles base_angle;
};
struct AimPunchState {
    std::optional<PredictableAimPunch> predictable;
    std::optional<UnpredictableAimPunch> unpredictable;
    std::optional<RecoilTimes> times;
};
struct CameraViewPunchState {
    Angles base_angle;
    GameTimePoint base_time;
    std::optional<float> evaluation_time_seconds, decay_exponent;
};
} // namespace cs2::model
