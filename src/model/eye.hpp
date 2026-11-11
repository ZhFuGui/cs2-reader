#pragma once
#include "model/types.hpp"
namespace cs2::model {
enum class EyeAngleSource : std::uint32_t {
    Network = 1,
    PredictionInput = 2,
    NativeInterpolation = 3,
    NativeHistory = 4,
    ParentTransformedInput = 5,
    Delegated = 6,
    WorldRotation = 7
};
struct EyeAngleBasis {
    Angles value;
    EyeAngleSource source;
    std::int32_t native_context;
    std::optional<float> interpolation_parameter;
    std::optional<EyeAngleSource> resolved_source = {};
    std::optional<EntityId> source_entity = {};
    std::uint32_t delegation_depth = 0;
};
} // namespace cs2::model
