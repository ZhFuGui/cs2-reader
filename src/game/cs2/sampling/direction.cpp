#include "game/cs2/sampling/direction.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "game/cs2/sampling/eye.hpp"
#include "game/cs2/sampling/recoil.hpp"

namespace cs2::game {
bool has_shared_context(const DirectionComponents& value) noexcept {
    return value.eye_basis && value.aim_punch && value.aim_punch->times && value.aim_punch->times->native_context &&
           value.eye_basis->native_context == *value.aim_punch->times->native_context;
}
bool has_compatible_context(const DirectionComponents& value) noexcept {
    if (!value.eye_basis || !value.aim_punch || !value.aim_punch->times || !value.aim_punch->times->native_context)
        return false;
    return value.eye_basis->source == model::EyeAngleSource::PredictionInput || has_shared_context(value);
}
namespace {
struct Attempt {
    DirectionComponents value;
    bool has_service = false;
};
Attempt current(memory::IReadChannel& reader, const Context& context, const Directory& directory,
                const EntityRef& pawn) {
    Attempt result;
    const auto service = memory::read_value<memory::Address>(
        reader, pawn.address + offsets::schema::C_CSPlayerPawn::m_pAimPunchServices);
    result.has_service = service && memory::user_range(*service);
    // 两组直接相邻，之间不插入ItemServices、武器或骨骼等无关读取。
    result.value.eye_basis = sample_eye_basis(reader, context, directory, pawn);
    if (result.has_service)
        result.value.aim_punch = sample_aim_punch(reader, context, directory, pawn, *service);
    return result;
}
} // namespace
DirectionComponents sample_direction(memory::IReadChannel& reader, const Context& context, const Directory& directory,
                                     const EntityRef& pawn) {
    auto attempt = current(reader, context, directory, pawn);
    const auto& punch = attempt.value.aim_punch;
    const bool complete_components = punch && punch->predictable && punch->unpredictable;
    if (attempt.has_service && (!has_compatible_context(attempt.value) || !complete_components)) {
        // 仅对这个组即时重采一次，避免在外部游戏不断切换上下文时无界等待。
        // 新尝试整体取代旧尝试，不能把两次读取的成功半组拼接或修改context来凑一致。
        attempt = current(reader, context, directory, pawn);
    }
    // 第二次仍缺失/冲突时如实发布已有字段，由消费者逐组判定；不沿用首尝试或上帧。
    return std::move(attempt.value);
}
} // namespace cs2::game
