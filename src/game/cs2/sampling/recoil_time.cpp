#include "game/cs2/sampling/recoil_time.hpp"
#include "game/cs2/entity/directory.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "game/cs2/recoil/profile.hpp"
#include "memory/block_reader.hpp"
#include "memory/read_plan.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cs2::game {
namespace {
namespace profile = recoil_profile;
using memory::Address;
using model::GameTimePoint;

std::optional<GameTimePoint> point(std::int64_t tick, float fraction) {
    if (!std::isfinite(fraction))
        return {};
    const auto whole = std::floor(static_cast<double>(fraction));
    if (whole < INT32_MIN || whole > INT32_MAX)
        return {};
    tick += static_cast<std::int64_t>(whole);
    if (tick < INT32_MIN || tick > INT32_MAX)
        return {};
    const auto remaining = fraction - static_cast<float>(whole);
    return GameTimePoint{static_cast<std::int32_t>(tick), remaining};
}
std::optional<GameTimePoint> seconds_point(float seconds) {
    if (!std::isfinite(seconds))
        return {};
    const auto remainder = std::fmod(seconds, profile::seconds_per_tick);
    const auto tick = (seconds - remainder) * 64.0f + .5f;
    if (!std::isfinite(tick) || static_cast<double>(tick) < INT32_MIN || static_cast<double>(tick) > INT32_MAX)
        return {};
    return point(static_cast<std::int32_t>(tick), remainder * 64.0f);
}
float seconds(GameTimePoint time) {
    return static_cast<float>(time.tick) * profile::seconds_per_tick + time.fraction * profile::seconds_per_tick;
}
struct WorldMapping {
    std::int32_t offset = 0, clamp = 0;
    bool clamp_enabled = false;
    std::optional<std::int32_t> map_tick(std::int32_t value) const {
        const auto tick = clamp_enabled && clamp > 0 && value >= clamp ? clamp : value;
        const auto mapped = static_cast<std::int64_t>(tick) - offset;
        if (mapped < INT32_MIN || mapped > INT32_MAX)
            return {};
        return static_cast<std::int32_t>(mapped);
    }
    float map_seconds(float value) const {
        if (clamp_enabled && clamp > 0 && value >= static_cast<float>(clamp) * profile::seconds_per_tick)
            return static_cast<float>(static_cast<std::int64_t>(clamp) - offset) * profile::seconds_per_tick;
        return value - static_cast<float>(offset) * profile::seconds_per_tick;
    }
};
struct NativeClock {
    Address globals = 0, prediction = 0, manager = 0;
    std::int32_t context = 0, global_tick = 0, prediction_tick = 0;
    float current_seconds = 0, frame_seconds = 0, global_fraction = 0, predicted_fraction = 0, simulation_fraction = 0;
    bool interpolation_disabled = false, prediction_suppressed = false;
    WorldMapping mapping;
    std::optional<GameTimePoint> prediction_time(bool interpolate) const {
        if (prediction_tick <= 0)
            return point(global_tick, 0);
        return point(prediction_tick, interpolate && frame_seconds != 0 ? predicted_fraction : 0);
    }
};
bool close_clock(memory::IReadChannel& reader, const Context& context, const NativeClock& clock,
                 std::optional<std::uint32_t> world_group) {
    Address globals{}, prediction{}, manager{}, table{};
    std::int32_t selector{}, offset{}, clamp{};
    std::uint8_t disabled{}, suppressed{}, enabled{};
    memory::ReadPlan plan;
    std::vector<memory::ReadPlan::Ticket> tickets;
    const auto add = [&](Address address, auto& value) { tickets.push_back(plan.add(address, value)); };
    add(context.client.base + offsets::client::dwGlobalVars, globals);
    add(context.client.base + profile::visual_context, selector);
    add(clock.globals + profile::interpolation_disabled, disabled);
    if (clock.prediction) {
        add(context.client.base + profile::prediction_root, prediction);
        add(clock.prediction + profile::prediction_suppressed, suppressed);
    }
    if (world_group && *world_group == 0) {
        add(context.client.base + profile::world_time_root, manager);
        if (clock.manager) {
            add(clock.manager, table);
            add(clock.manager + profile::world_offset, offset);
            add(clock.manager + profile::world_clamp, clamp);
            add(clock.manager + profile::world_clamp_enabled, enabled);
        }
    }
    plan.execute(reader);
    if (!std::all_of(tickets.begin(), tickets.end(), [&](auto t) { return plan.complete(t); }) ||
        globals != clock.globals || selector != clock.context || (disabled != 0) != clock.interpolation_disabled ||
        (clock.prediction && (prediction != clock.prediction || (suppressed != 0) != clock.prediction_suppressed)))
        return false;
    if (world_group && *world_group == 0) {
        if (manager != clock.manager)
            return false;
        if (manager && (!memory::user_range(table) ||
                        context.static_data.value<Address>(reader, table + 0x210) !=
                            std::optional(context.client.base + profile::world_tick_function) ||
                        context.static_data.value<Address>(reader, table + 0x220) !=
                            std::optional(context.client.base + profile::world_seconds_function) ||
                        offset != clock.mapping.offset || clamp != clock.mapping.clamp ||
                        (enabled != 0) != clock.mapping.clamp_enabled))
            return false;
    }
    return true;
}
std::optional<NativeClock> read_clock(memory::IReadChannel& reader, const Context& context,
                                      std::optional<std::uint32_t> world_group, bool need_prediction) {
    if (context.build != profile::build)
        return {};
    NativeClock clock;
    const auto globals = memory::read_value<Address>(reader, context.client.base + offsets::client::dwGlobalVars);
    const auto selector = memory::read_value<std::int32_t>(reader, context.client.base + profile::visual_context);
    if (!globals || !selector || !memory::user_range(*globals))
        return {};
    clock.globals = *globals;
    clock.context = *selector;
    memory::Block values;
    values.read(reader, *globals + profile::current_seconds,
                profile::simulation_fraction + sizeof(float) - profile::current_seconds);
    const auto real = [&](auto offset) -> std::optional<float> {
        auto value = values.get<float>(offset - profile::current_seconds);
        return value && std::isfinite(*value) ? value : std::nullopt;
    };
    const auto cur = real(profile::current_seconds), frame = real(profile::frame_seconds),
               fraction = real(profile::global_fraction), predicted = real(profile::predicted_fraction),
               simulation = real(profile::simulation_fraction);
    const auto tick = values.get<std::int32_t>(profile::global_tick - profile::current_seconds);
    const auto disabled = values.get<std::uint8_t>(profile::interpolation_disabled - profile::current_seconds);
    if (!cur || !frame || !fraction || !predicted || !simulation || !tick || !disabled)
        return {};
    clock.current_seconds = *cur;
    clock.frame_seconds = *frame;
    clock.global_fraction = *fraction;
    clock.predicted_fraction = *predicted;
    clock.simulation_fraction = *simulation;
    clock.global_tick = *tick;
    clock.interpolation_disabled = *disabled != 0;
    if (need_prediction) {
        const auto prediction = memory::read_value<Address>(reader, context.client.base + profile::prediction_root);
        if (!prediction || !memory::user_range(*prediction))
            return {};
        clock.prediction = *prediction;
        const auto suppressed = memory::read_value<std::uint8_t>(reader, *prediction + profile::prediction_suppressed);
        const auto predicted_tick = memory::read_value<std::int32_t>(reader, *prediction + profile::prediction_tick);
        if (!suppressed || !predicted_tick)
            return {};
        clock.prediction_suppressed = *suppressed != 0;
        clock.prediction_tick = *predicted_tick;
    }
    if (world_group && *world_group == 0) {
        const auto manager = memory::read_value<Address>(reader, context.client.base + profile::world_time_root);
        if (!manager)
            return {};
        clock.manager = *manager;
        if (*manager) {
            if (!memory::user_range(*manager))
                return {};
            const auto table = memory::read_value<Address>(reader, *manager);
            if (!table || !memory::user_range(*table) ||
                context.static_data.value<Address>(reader, *table + 0x210) !=
                    std::optional(context.client.base + profile::world_tick_function) ||
                context.static_data.value<Address>(reader, *table + 0x220) !=
                    std::optional(context.client.base + profile::world_seconds_function))
                return {};
            struct Mapping {
                std::int32_t offset, clamp;
                std::uint8_t enabled;
            };
            const auto mapping = memory::read_value<Mapping>(reader, *manager + profile::world_offset);
            if (!mapping)
                return {};
            clock.mapping = {mapping->offset, mapping->clamp, mapping->enabled != 0};
        }
    }
    if (memory::read_value<Address>(reader, context.client.base + offsets::client::dwGlobalVars) != globals ||
        memory::read_value<std::int32_t>(reader, context.client.base + profile::visual_context) != selector ||
        (clock.prediction && memory::read_value<Address>(reader, context.client.base + profile::prediction_root) !=
                                 std::optional(clock.prediction)) ||
        (world_group && *world_group == 0 &&
         memory::read_value<Address>(reader, context.client.base + profile::world_time_root) !=
             std::optional(clock.manager)))
        return {};
    return clock;
}
} // namespace

std::optional<model::RecoilTimes> sample_recoil_times(memory::IReadChannel& reader, const Context& context,
                                                      const Directory& directory, const EntityRef& pawn) {
    const auto group =
        memory::read_value<std::uint32_t>(reader, pawn.identity + offsets::schema::CEntityIdentity::m_worldGroupId);
    const auto predictable =
        memory::read_value<std::uint8_t>(reader, pawn.address + offsets::schema::C_BaseEntity::m_bPredictable);
    if (!group || !predictable)
        return {};
    const auto clock = read_clock(reader, context, *group, *predictable != 0);
    if (!clock)
        return {};
    model::RecoilTimes result{*group,
                              clock->context == -1 ? model::VisualRecoilTimePolicy::SeparateRenderTime
                                                   : model::VisualRecoilTimePolicy::SharedSimulationTime,
                              {},
                              {},
                              {},
                              clock->context};
    const auto tick = clock->mapping.map_tick(clock->global_tick);
    if (tick)
        result.simulation = point(static_cast<std::int64_t>(*tick) + 1, clock->simulation_fraction);
    bool controller_failed = false, controller_time_used = false;
    if (*predictable && !clock->prediction_suppressed) {
        const auto handle =
            memory::read_value<std::uint32_t>(reader, pawn.address + offsets::schema::C_BasePlayerPawn::m_hController);
        if (!handle)
            controller_failed = true;
        else if (*handle != UINT32_MAX && *handle != UINT32_MAX - 1) {
            const auto* controller = directory.resolve(*handle);
            if (!controller || controller->kind != EntityKind::Controller || !directory.verify(reader, *controller))
                controller_failed = true;
            else {
                const auto cp = memory::read_value<std::uint8_t>(
                    reader, controller->address + offsets::schema::C_BaseEntity::m_bPredictable);
                auto time =
                    cp ? (*cp ? clock->prediction_time(!clock->interpolation_disabled)
                              : point(clock->global_tick, clock->interpolation_disabled ? 0 : clock->global_fraction))
                       : std::nullopt;
                if (time) {
                    auto mapped = clock->mapping.map_tick(time->tick);
                    time = mapped ? point(*mapped, time->fraction) : std::nullopt;
                }
                if (!time || !directory.verify(reader, *controller) ||
                    memory::read_value<std::uint8_t>(reader, controller->address +
                                                                 offsets::schema::C_BaseEntity::m_bPredictable) != cp ||
                    memory::read_value<std::uint32_t>(
                        reader, pawn.address + offsets::schema::C_BasePlayerPawn::m_hController) != handle)
                    controller_failed = true;
                else {
                    result.simulation = time;
                    controller_time_used = true;
                }
            }
        }
    }
    const auto render = seconds_point(clock->mapping.map_seconds(clock->current_seconds));
    if (controller_failed)
        result.simulation.reset();
    if (clock->context == -1) {
        result.visual_unpredictable = render;
        result.visual_predictable = controller_failed      ? std::nullopt
                                    : controller_time_used ? result.simulation
                                                           : render;
    } else
        result.visual_predictable = result.visual_unpredictable = result.simulation;
    if (memory::read_value<std::uint32_t>(reader, pawn.identity + offsets::schema::CEntityIdentity::m_worldGroupId) !=
            group ||
        memory::read_value<std::uint8_t>(reader, pawn.address + offsets::schema::C_BaseEntity::m_bPredictable) !=
            predictable ||
        !close_clock(reader, context, *clock, *group))
        return {};
    return result;
}

std::optional<float> sample_view_punch_time(memory::IReadChannel& reader, const Context& context, const EntityRef& pawn,
                                            std::uint8_t predictable) {
    const auto clock = read_clock(reader, context, std::nullopt, predictable != 0);
    if (!clock)
        return {};
    std::optional<float> result;
    if (predictable && !clock->prediction_suppressed) {
        if (const auto time = clock->prediction_time(!clock->interpolation_disabled))
            result = seconds(*time);
    } else
        result = clock->current_seconds;
    if (!close_clock(reader, context, *clock, std::nullopt) ||
        memory::read_value<std::uint8_t>(reader, pawn.address + offsets::schema::C_BaseEntity::m_bPredictable) !=
            predictable)
        return {};
    return result;
}
} // namespace cs2::game
