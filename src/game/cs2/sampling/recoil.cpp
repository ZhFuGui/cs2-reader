#include "game/cs2/sampling/recoil.hpp"
#include "game/cs2/entity/identity_observation.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "game/cs2/recoil/profile.hpp"
#include "game/cs2/sampling/common.hpp"
#include "game/cs2/sampling/recoil_time.hpp"
#include <cmath>
#include <limits>

namespace cs2::game {
namespace {
namespace profile = recoil_profile;
using memory::Address;

} // namespace

std::optional<model::AimPunchState> sample_aim_punch(memory::IReadChannel& reader, const Context& context,
                                                     const Directory& directory, const EntityRef& pawn,
                                                     Address service) {
    if (!memory::user_range(service) || context.build != profile::build)
        return {};
    namespace native = offsets::schema::CCSPlayer_AimPunchServices;
    using memory::field;
    const memory::Fields fields(
        reader, service,
        {field<std::int32_t>(native::m_predictableBaseTick), field<float>(native::m_predictableBaseTickInterpAmount),
         field<model::Angles>(native::m_predictableBaseAngle), field<model::Angles>(native::m_predictableBaseAngleVel),
         field<std::int32_t>(native::m_unpredictableBaseTick), field<model::Angles>(native::m_unpredictableBaseAngle)});
    model::AimPunchState result;
    const auto p_tick = fields.get<std::int32_t>(native::m_predictableBaseTick),
               u_tick = fields.get<std::int32_t>(native::m_unpredictableBaseTick);
    const auto fraction = real(fields, native::m_predictableBaseTickInterpAmount);
    const auto p_angle = angles(fields, native::m_predictableBaseAngle),
               velocity = angles(fields, native::m_predictableBaseAngleVel),
               u_angle = angles(fields, native::m_unpredictableBaseAngle);
    if (p_tick && fraction && p_angle && velocity)
        result.predictable = model::PredictableAimPunch{{*p_tick, *fraction}, *p_angle, *velocity};
    if (u_tick && u_angle)
        result.unpredictable = model::UnpredictableAimPunch{*u_tick, *u_angle};
    const auto owner = memory::read_value<Address>(reader, service + profile::owner);
    if (owner && *owner == pawn.address)
        result.times = sample_recoil_times(reader, context, directory, pawn);
    static_assert(sizeof(model::PredictableAimPunch) == 32 && sizeof(model::UnpredictableAimPunch) == 16);
    model::PredictableAimPunch predictable{};
    model::UnpredictableAimPunch unpredictable{};
    Address closing_service{}, closing_owner{};
    memory::ReadPlan closing;
    std::optional<memory::ReadPlan::Ticket> p_ticket, u_ticket;
    if (result.predictable)
        p_ticket = closing.add(service + native::m_predictableBaseTick, predictable);
    if (result.unpredictable)
        u_ticket = closing.add(service + native::m_unpredictableBaseTick, unpredictable);
    const auto service_ticket =
        closing.add(pawn.address + offsets::schema::C_CSPlayerPawn::m_pAimPunchServices, closing_service);
    const auto owner_ticket = closing.add(service + profile::owner, closing_owner);
    IdentityObservation identity(pawn);
    identity.schedule(closing);
    closing.execute(reader);
    if (p_ticket &&
        (!closing.complete(*p_ticket) || std::memcmp(&predictable, &*result.predictable, sizeof(predictable)) != 0))
        result.predictable.reset();
    if (u_ticket && (!closing.complete(*u_ticket) ||
                     std::memcmp(&unpredictable, &*result.unpredictable, sizeof(unpredictable)) != 0))
        result.unpredictable.reset();
    const auto actual_owner = closing.complete(owner_ticket) ? std::optional(closing_owner) : std::nullopt;
    if ((owner && *owner && *owner != pawn.address) || !closing.complete(service_ticket) ||
        closing_service != service || actual_owner != owner || !identity.matches(closing))
        return {};
    if (!result.predictable && !result.unpredictable)
        return {};
    return result;
}
std::optional<model::CameraViewPunchState> sample_camera_view_punch(memory::IReadChannel& reader,
                                                                    const Context& context, const Directory& directory,
                                                                    const EntityRef& pawn, Address service) {
    if (!memory::user_range(service) || context.build != profile::build)
        return {};
    namespace native = offsets::schema::CPlayer_CameraServices;
    using memory::field;
    const memory::Fields fields(reader, service,
                                {field<model::Angles>(native::m_vecCsViewPunchAngle),
                                 field<std::int32_t>(native::m_nCsViewPunchAngleTick),
                                 field<float>(native::m_flCsViewPunchAngleTickRatio)});
    const auto angle = angles(fields, native::m_vecCsViewPunchAngle);
    const auto tick = fields.get<std::int32_t>(native::m_nCsViewPunchAngleTick);
    const auto fraction = real(fields, native::m_flCsViewPunchAngleTickRatio);
    if (!angle || !tick || !fraction)
        return {};
    model::CameraViewPunchState result{*angle, {*tick, *fraction}, {}, {}};
    const auto predictable =
        memory::read_value<std::uint8_t>(reader, pawn.address + offsets::schema::C_BaseEntity::m_bPredictable);
    const auto owner = memory::read_value<Address>(reader, service + profile::owner);
    // view-punch使用全局秒时间，不经过AimPunch的WorldGroup时间映射。
    if (predictable && owner && *owner == pawn.address)
        result.evaluation_time_seconds = sample_view_punch_time(reader, context, pawn, *predictable);
    const auto data =
        memory::read_value<Address>(reader, context.client.base + profile::view_punch_decay + profile::cvar_data);
    if (data && memory::user_range(*data)) {
        const auto decay = memory::read_value<float>(reader, *data + profile::cvar_value);
        if (decay && std::isfinite(*decay) &&
            memory::read_value<Address>(reader, context.client.base + profile::view_punch_decay + profile::cvar_data) ==
                data &&
            memory::read_value<float>(reader, *data + profile::cvar_value) == decay)
            result.decay_exponent = *decay;
    }
    struct ViewState {
        model::Angles angle;
        model::GameTimePoint time;
    };
    static_assert(sizeof(ViewState) == 20);
    const ViewState original{*angle, {*tick, *fraction}};
    const auto closing = memory::read_value<ViewState>(reader, service + native::m_vecCsViewPunchAngle);
    if (!closing || std::memcmp(&*closing, &original, sizeof(original)) != 0 ||
        (owner && *owner && *owner != pawn.address) ||
        memory::read_value<Address>(reader, service + profile::owner) != owner ||
        memory::read_value<Address>(reader, pawn.address + offsets::schema::C_BasePlayerPawn::m_pCameraServices) !=
            std::optional(service) ||
        !directory.verify(reader, pawn))
        return {};
    return result;
}
} // namespace cs2::game
