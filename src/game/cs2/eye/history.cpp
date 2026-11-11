#include "game/cs2/eye/history.hpp"
#include "game/cs2/eye/evaluation.hpp"
#include "game/cs2/eye/profile.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include <array>
namespace cs2::game::eye {
namespace {
struct Lane {
    memory::Address buffer;
    std::uint32_t packed, extra;
    std::int32_t progress_tick;
    std::uint32_t bookkeeping;
    float delay;
    std::uint32_t allocation;
};
static_assert(sizeof(Lane) == 32);
} // namespace
std::optional<model::EyeAngleBasis> history_basis(Observation& read, const Context& context, memory::Address pawn,
                                                  std::int32_t native_context) {
    namespace p = eye_profile;
    const auto active = read.get<std::uint8_t>(pawn + p::active);
    if (!active)
        return {};
    int lane = -1;
    std::optional<std::uint8_t> flags;
    if (*active) {
        const auto mode = read.get<std::int32_t>(context.client.base + p::lane_mode);
        flags = read.get<std::uint8_t>(pawn + p::flags);
        if (!mode || !flags)
            return {};
        if (native_context == -1 || (native_context == 0 && *mode == 1))
            lane = 0;
        else if ((native_context == 0 && (*flags & 32)) || (native_context == 1 && *mode == 1))
            lane = 1;
        if (lane == 1 && !(*flags & 32))
            lane = 0;
    }
    if (lane < 0) {
        const auto value = read.get<model::Angles>(pawn + offsets::schema::C_CSPlayerPawn::m_angEyeAngles);
        if (!value || !model::finite(*value))
            return {};
        return model::EyeAngleBasis{*value, model::EyeAngleSource::Network, native_context, {}};
    }
    const auto parameter = read.get<float>(context.client.base + p::time + (*flags >> 7) * 4);
    const auto cache_time = read.get<float>(pawn + p::cache_time + lane * 4);
    if (!parameter || !cache_time || !std::isfinite(*parameter) || !std::isfinite(*cache_time))
        return {};
    bool solve = false;
    if (*parameter != *cache_time) {
        const auto dirty = read.get<std::uint8_t>(pawn + p::dirty);
        if (!dirty)
            return {};
        solve = *parameter < *cache_time || (*dirty & (1u << (6 + lane)));
    }
    if (!solve) {
        const auto value = read.get<model::Angles>(pawn + p::cache_value + lane * 12);
        if (!value || !model::finite(*value))
            return {};
        return model::EyeAngleBasis{*value, model::EyeAngleSource::NativeInterpolation, native_context, parameter};
    }
    const auto history = pawn + p::history;
    const auto table = read.get<memory::Address>(history);
    if (!table || *table != context.client.base + p::history_vtable ||
        read.get<memory::Address>(*table + 0x98) != std::optional(context.client.base + p::history_fallback) ||
        read.get<memory::Address>(*table + 0x128) != std::optional(context.client.base + p::history_type) ||
        read.get<memory::Address>(*table + 0x18) != std::optional(context.client.base + p::history_delay) ||
        read.get<memory::Address>(*table + 0x90) != std::optional(context.client.base + p::history_after_fallback))
        return {};
    const auto lanes = read.get<memory::Address>(history + 0x20);
    const auto type = read.get<std::uint8_t>(history + 8);
    if (!lanes || !memory::user_range(*lanes) || !type)
        return {};
    const auto state = read.get<Lane>(*lanes + lane * sizeof(Lane));
    if (!state || !std::isfinite(state->delay))
        return {};
    const std::size_t count = (state->packed >> 13) & 63, capacity = (state->packed >> 19) & 63;
    const std::size_t head = state->packed & 63, stride = (state->packed >> 6) & 63;
    std::vector<HistorySample> samples;
    if (count) {
        // 六位计数来自该原生容器格式；不是Reader人为设置的采集配额。
        if (!capacity || count > capacity || head >= capacity || stride != 1)
            return {};
        const auto ring = read.bytes(state->buffer, capacity * (8 + sizeof(model::Angles)));
        if (!ring)
            return {};
        samples.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto offset = ((head + i) % capacity) * 8;
            std::int32_t tick;
            std::int16_t index;
            std::memcpy(&tick, ring->data() + offset, sizeof(tick));
            std::memcpy(&index, ring->data() + offset + 4, sizeof(index));
            if (index < 0 || static_cast<std::size_t>(index) >= capacity)
                return {};
            model::Angles value;
            std::memcpy(&value, ring->data() + capacity * 8 + static_cast<std::size_t>(index) * sizeof(value),
                        sizeof(value));
            samples.push_back({tick, value});
        }
    }
    model::Angles fallback{};
    if (!count) {
        const auto source = read.get<memory::Address>(history + 0x18);
        if (!source || *source != pawn + offsets::schema::C_CSPlayerPawn::m_angEyeAngles)
            return {};
        const auto value = read.get<model::Angles>(*source);
        if (!value)
            return {};
        fallback = *value;
    }
    const auto no_delay = read.get<std::uint8_t>(context.client.base + p::no_history_delay);
    const auto extrapolate = read.get<std::uint8_t>(context.client.base + p::extrapolation_enabled);
    const auto minimum = read.get<std::int32_t>(context.client.base + p::extrapolation_min_tick);
    const auto limit = read.get<float>(context.client.base + p::extrapolation_limit);
    if (!no_delay || !extrapolate || !minimum || !limit)
        return {};
    const HistoryPolicy policy{*parameter,     state->delay,      *limit,          *minimum,
                               *no_delay != 0, *extrapolate != 0, (*type & 1) != 0};
    const auto value = evaluate_history(samples, policy, fallback);
    if (!value)
        return {};
    return model::EyeAngleBasis{*value, model::EyeAngleSource::NativeHistory, native_context, parameter};
}
} // namespace cs2::game::eye
