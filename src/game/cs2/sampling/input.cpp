#include "game/cs2/sampling/input.hpp"
#include "game/cs2/input/profile.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "memory/block_reader.hpp"
#include <cmath>

namespace cs2::game {
namespace {
namespace profile = input_profile;
using memory::Address;
template <class T> struct CvarSample {
    Address root = 0, data = 0, slots = 0;
    std::uint32_t flags = 0;
    T value{};
};
template <class T>
std::optional<CvarSample<T>> cvar(memory::IReadChannel& reader, const Context& context, std::size_t offset) {
    CvarSample<T> result;
    result.root = context.client.base + offset;
    const auto data = memory::read_value<Address>(reader, result.root + profile::cvar_data);
    if (!data || !memory::user_range(*data))
        return {};
    result.data = *data;
    const memory::Fields header(
        reader, *data,
        {memory::field<std::uint16_t>(profile::cvar_type), memory::field<std::uint32_t>(profile::cvar_flags)});
    const auto type = header.get<std::uint16_t>(profile::cvar_type);
    const auto flags = header.get<std::uint32_t>(profile::cvar_flags);
    constexpr auto expected_type = std::is_same_v<T, float> ? 7 : 0;
    if (!type || *type != expected_type || !flags)
        return {};
    result.flags = *flags;
    if (*flags & (1u << 15)) {
        const auto slots = memory::read_value<Address>(reader, context.client.base + profile::slot_manager);
        if (!slots || !memory::user_range(*slots))
            return {};
        result.slots = *slots;
        const auto count = memory::read_value<std::int32_t>(reader, *slots + profile::slot_count);
        if (!count || *count <= 0)
            return {};
    }
    const auto value = memory::read_value<T>(reader, *data + profile::cvar_value);
    if (!value)
        return {};
    if constexpr (std::is_same_v<T, float>) {
        if (!std::isfinite(*value))
            return {};
    }
    result.value = *value;
    return result;
}
template <class T>
std::optional<T> close(memory::IReadChannel& reader, const Context& context,
                       const std::optional<CvarSample<T>>& sample) {
    if (!sample)
        return {};
    const auto& v = *sample;
    if (memory::read_value<Address>(reader, v.root + profile::cvar_data) != std::optional(v.data) ||
        memory::read_value<std::uint32_t>(reader, v.data + profile::cvar_flags) != std::optional(v.flags) ||
        memory::read_value<T>(reader, v.data + profile::cvar_value) != std::optional(v.value))
        return {};
    if (v.slots) {
        const auto count = memory::read_value<std::int32_t>(reader, v.slots + profile::slot_count);
        if (memory::read_value<Address>(reader, context.client.base + profile::slot_manager) !=
                std::optional(v.slots) ||
            !count || *count <= 0)
            return {};
    }
    return v.value;
}
bool input_profile_matches(memory::IReadChannel& reader, const Context& context, Address table) {
    return memory::user_range(table) &&
           memory::read_value<Address>(reader, context.client.base + offsets::client::dwCSGOInput) ==
               std::optional(table) &&
           memory::read_value<Address>(reader, table + 0xb0) ==
               std::optional(context.client.base + profile::input_motion_function) &&
           memory::read_value<Address>(reader, table + 0xc0) ==
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
    const auto sensitivity = cvar<float>(reader, context, profile::sensitivity);
    const auto vertical = cvar<float>(reader, context, profile::vertical_scale);
    const auto yaw = cvar<float>(reader, context, profile::yaw), pitch = cvar<float>(reader, context, profile::pitch);
    const auto zoom = cvar<float>(reader, context, profile::zoom_ratio);
    const auto invert = cvar<std::uint8_t>(reader, context, profile::invert_y);
    const auto cheats = cvar<std::uint8_t>(reader, context, profile::cheats);
    const auto source_x = memory::read_value<std::uint32_t>(reader, context.client.base + profile::source_x);
    const auto source_y = memory::read_value<std::uint32_t>(reader, context.client.base + profile::source_y);
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
    result.sensitivity = close(reader, context, sensitivity);
    result.vertical_scale = close(reader, context, vertical);
    result.yaw = close(reader, context, yaw);
    result.pitch = close(reader, context, pitch);
    result.zoom_ratio = close(reader, context, zoom);
    if (const auto value = close(reader, context, invert))
        result.inverted_y = *value != 0;
    if (const auto value = close(reader, context, cheats)) {
        if (cheats->flags & (1u << 10))
            result.effective_pitch = .022f;
        else if (*value)
            result.effective_pitch = result.pitch;
        else if (result.pitch)
            result.effective_pitch = *result.pitch > 0 ? .022f : -.022f;
    }
    // 不能确认sv_cheats来源时留空；不把读取失败解释为原生空指针分支。
    if (source_x && source_y &&
        memory::read_value<std::uint32_t>(reader, context.client.base + profile::source_x) == source_x &&
        memory::read_value<std::uint32_t>(reader, context.client.base + profile::source_y) == source_y) {
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
    if (const auto value = close(reader, context, cvar<std::uint8_t>(reader, context, profile::crosshair_recoil)))
        return *value != 0;
    return {};
}
} // namespace cs2::game
