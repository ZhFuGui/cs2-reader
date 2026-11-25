#include "game/cs2/sampling/input.hpp"
#include "game/cs2/input/profile.hpp"
#include "game/cs2/input/cvar.hpp"
#include "memory/read_plan.hpp"
#include <array>
#include <algorithm>
#include "game/cs2/offsets/catalog.hpp"
#include "memory/block_reader.hpp"
#include <cmath>

namespace cs2::game {
namespace {
namespace profile = input_profile;
using memory::Address;
struct SlotSource {
    Address manager = 0;
    bool required = false, valid = true;
};
SlotSource slots(memory::IReadChannel& reader, const Context& context, const CvarBatch& batch, std::size_t count) {
    SlotSource result;
    for (std::size_t i = 0; i < count; ++i)
        result.required |= batch.at(i).readable && (batch.at(i).flags & (1u << 15));
    if (!result.required)
        return result;
    memory::ReadPlan plan;
    const auto root = plan.pointer(context.client.base + profile::slot_manager);
    std::int32_t size{};
    const auto value = plan.add_from(root, profile::slot_count, size);
    plan.execute(reader);
    const auto manager = plan.pointer_value(root);
    result.valid = manager && memory::user_range(*manager) && plan.complete(value) && size > 0;
    if (manager)
        result.manager = *manager;
    return result;
}
bool close_slots(memory::IReadChannel& reader, const Context& context, const SlotSource& source) {
    if (!source.required)
        return true;
    if (!source.valid)
        return false;
    memory::ReadPlan plan;
    Address manager{};
    std::int32_t count{};
    const auto root = plan.add(context.client.base + profile::slot_manager, manager);
    const auto value = plan.add(source.manager + profile::slot_count, count);
    plan.execute(reader);
    return plan.complete(root) && plan.complete(value) && manager == source.manager && count > 0;
}
bool input_profile_matches(memory::IReadChannel& reader, const Context& context, Address table) {
    return memory::user_range(table) &&
           memory::read_value<Address>(reader, context.client.base + offsets::client::dwCSGOInput) ==
               std::optional(table) &&
           context.static_data.value<Address>(reader, table + 0xb0) ==
               std::optional(context.client.base + profile::input_motion_function) &&
           context.static_data.value<Address>(reader, table + 0xc0) ==
               std::optional(context.client.base + profile::input_scale_function);
}
} // namespace

std::optional<model::MouseMapping> sample_mouse_mapping(memory::IReadChannel& reader, const Context& context,
                                                        const Directory& directory, const EntityRef* pawn) {
    if (context.build != profile::build)
        return {};
    const auto table = memory::read_value<Address>(reader, context.client.base + offsets::client::dwCSGOInput);
    if (!table || !input_profile_matches(reader, context, *table))
        return {};
    constexpr std::array requests{
        CvarRequest{profile::sensitivity, true}, CvarRequest{profile::vertical_scale, true},
        CvarRequest{profile::yaw, true}, CvarRequest{profile::pitch, true},
        CvarRequest{profile::zoom_ratio, true}, CvarRequest{profile::invert_y, false},
        CvarRequest{profile::cheats, false}};
    CvarBatch settings(reader, context, requests);
    const auto slot_source = slots(reader, context, settings, requests.size());
    std::uint32_t x{}, y{};
    memory::ReadPlan sources;
    const auto x_ticket = sources.add(context.client.base + profile::source_x, x);
    const auto y_ticket = sources.add(context.client.base + profile::source_y, y);
    sources.execute(reader);
    const auto source_x = sources.complete(x_ticket) ? std::optional(x) : std::nullopt;
    const auto source_y = sources.complete(y_ticket) ? std::optional(y) : std::nullopt;
    model::MouseMapping result;
    namespace base = offsets::schema::C_BasePlayerPawn;
    const auto local_controller =
        memory::read_value<Address>(reader, context.client.base + offsets::client::dwLocalPlayerController);
    const auto* controller = local_controller ? directory.find_address(*local_controller) : nullptr;
    std::optional<std::uint32_t> pawn_handle;
    if (pawn && controller && controller->kind == EntityKind::Controller && directory.verify(reader, *controller)) {
        pawn_handle = memory::read_value<std::uint32_t>(reader, controller->address +
                                                                    offsets::schema::CBasePlayerController::m_hPawn);
        if (pawn_handle == std::optional(pawn->handle) && directory.verify(reader, *pawn)) {
            const memory::Fields values(reader, pawn->address,
                                        {memory::field<float>(base::m_flFOVSensitivityAdjust),
                                         memory::field<float>(base::m_flMouseSensitivity)});
            const auto factor = values.get<float>(base::m_flFOVSensitivityAdjust);
            const auto override_value = values.get<float>(base::m_flMouseSensitivity);
            if (factor && std::isfinite(*factor))
                result.pawn_fov_adjust = *factor;
            if (override_value && std::isfinite(*override_value))
                result.pawn_mouse_sensitivity = *override_value;
            if (!directory.verify(reader, *pawn) || !directory.verify(reader, *controller) ||
                memory::read_value<std::uint32_t>(
                    reader, controller->address + offsets::schema::CBasePlayerController::m_hPawn) != pawn_handle ||
                memory::read_value<Address>(reader, context.client.base + offsets::client::dwLocalPlayerController) !=
                    local_controller ||
                memory::read_value<float>(reader, pawn->address + base::m_flFOVSensitivityAdjust) !=
                    result.pawn_fov_adjust ||
                memory::read_value<float>(reader, pawn->address + base::m_flMouseSensitivity) !=
                    result.pawn_mouse_sensitivity) {
                result.pawn_fov_adjust.reset();
                result.pawn_mouse_sensitivity.reset();
            }
        }
    }
    const auto matches = settings.close(reader);
    const bool slots_valid = close_slots(reader, context, slot_source);
    const auto valid = [&](std::size_t i) {
        return matches[i] && (slots_valid || !(settings.at(i).flags & (1u << 15)));
    };
    if (valid(0)) result.sensitivity = settings.at(0).real();
    if (valid(1)) result.vertical_scale = settings.at(1).real();
    if (valid(2)) result.yaw = settings.at(2).real();
    if (valid(3)) result.pitch = settings.at(3).real();
    if (valid(4)) result.zoom_ratio = settings.at(4).real();
    if (const auto value = valid(5) ? settings.at(5).boolean() : std::nullopt)
        result.inverted_y = *value != 0;
    if (const auto value = valid(6) ? settings.at(6).boolean() : std::nullopt) {
        if (settings.at(6).flags & (1u << 10))
            result.effective_pitch = .022f;
        else if (*value)
            result.effective_pitch = result.pitch;
        else if (result.pitch)
            result.effective_pitch = *result.pitch > 0 ? .022f : -.022f;
    }
    // 来源选择单独关闭，不把任一配置的失败扩散到其他独立结果。
    std::uint32_t closing_x{}, closing_y{};
    memory::ReadPlan closing_sources;
    const auto cx = closing_sources.add(context.client.base + profile::source_x, closing_x);
    const auto cy = closing_sources.add(context.client.base + profile::source_y, closing_y);
    closing_sources.execute(reader);
    if (source_x && source_y && closing_sources.complete(cx) && closing_sources.complete(cy) &&
        closing_x == *source_x && closing_y == *source_y) {
        const auto selected = (*source_y & ~1u) <= (*source_x & ~1u) ? *source_x : *source_y;
        result.source = (selected & 1) ? model::NativeInputSource::Analog : model::NativeInputSource::Mouse;
    }
    if (!input_profile_matches(reader, context, *table))
        return {};
    if (result.source == model::NativeInputSource::Mouse && result.sensitivity && result.pawn_fov_adjust) {
        const auto fov = *result.pawn_fov_adjust > 0 ? *result.pawn_fov_adjust : 1.f;
        if (result.yaw) {
            const auto value = *result.yaw * (fov * *result.sensitivity) * -1.f;
            if (std::isfinite(value))
                result.yaw_degrees_per_delta = value;
        }
        if (result.effective_pitch && result.vertical_scale && result.inverted_y) {
            const auto value = *result.effective_pitch * (*result.vertical_scale * fov * *result.sensitivity) *
                               (*result.inverted_y ? -1.f : 1.f);
            if (std::isfinite(value))
                result.pitch_degrees_per_delta = value;
        }
    }
    return result;
}
std::optional<bool> sample_crosshair_recoil(memory::IReadChannel& reader, const Context& context) {
    if (context.build != profile::build)
        return {};
    constexpr std::array requests{CvarRequest{profile::crosshair_recoil, false}};
    CvarBatch settings(reader, context, requests);
    const auto slot_source = slots(reader, context, settings, requests.size());
    const auto matches = settings.close(reader);
    if (matches[0] && close_slots(reader, context, slot_source))
        if (const auto value = settings.at(0).boolean())
            return *value != 0;
    return {};
}
} // namespace cs2::game
