#include "game/cs2/sampling/player.hpp"
#include "game/cs2/entity/layout.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "game/cs2/sampling/common.hpp"
#include "game/cs2/sampling/direction.hpp"
#include <algorithm>

namespace cs2::game {
memory::Fields body_fields(memory::Address address) {
    namespace base = offsets::schema::C_BasePlayerPawn;
    namespace pawn = offsets::schema::C_CSPlayerPawn;
    namespace pawnbase = offsets::schema::C_CSPlayerPawnBase;
    namespace modelentity = offsets::schema::C_BaseModelEntity;
    using memory::field;
    return memory::Fields(
        address, entity_fields(),
        {field<memory::Address>(base::m_pWeaponServices),
         field<memory::Address>(base::m_pItemServices),
         field<model::Vec3>(modelentity::m_vecViewOffset),
         field<std::int32_t>(pawn::m_ArmorValue),
         field<std::int32_t>(pawn::m_iShotsFired),
         field<std::uint8_t>(pawn::m_bIsScoped),
         field<std::uint8_t>(pawn::m_bIsDefusing),
         field<float>(pawnbase::m_flFlashDuration),
         field<float>(pawnbase::m_flFlashMaxAlpha),
         field<model::Angles>(pawn::m_angEyeAngles),
         field<model::Angles>(base::v_angle),
         field<std::uint8_t>(pawn::m_entitySpottedState + offsets::schema::EntitySpottedState_t::m_bSpotted),
         field<std::array<std::uint32_t, 2>>(pawn::m_entitySpottedState +
                                             offsets::schema::EntitySpottedState_t::m_bSpottedByMask),
         {pawn::m_szLastPlaceName, 18}});
}
namespace {
model::PlayerBody sample_body(memory::IReadChannel& reader, const Context& context, const Directory& directory,
                              const EntitySample& sample) {
    namespace base = offsets::schema::C_BasePlayerPawn;
    namespace pawn = offsets::schema::C_CSPlayerPawn;
    namespace pawnbase = offsets::schema::C_CSPlayerPawnBase;
    namespace modelentity = offsets::schema::C_BaseModelEntity;
    using memory::field;
    const auto& ref = sample.ref;
    const auto& fields = sample.fields;
    model::PlayerBody result;
    result.entity = project_common(sample);
    result.armor = fields.get<std::int32_t>(pawn::m_ArmorValue);
    result.shots_fired = fields.get<std::int32_t>(pawn::m_iShotsFired);
    result.scoped = boolean(fields, pawn::m_bIsScoped);
    result.defusing = boolean(fields, pawn::m_bIsDefusing);
    result.flash_duration = real(fields, pawnbase::m_flFlashDuration);
    result.flash_max_alpha = real(fields, pawnbase::m_flFlashMaxAlpha);
    result.eye_angles = angles(fields, pawn::m_angEyeAngles);
    result.input_eye_angles = angles(fields, base::v_angle);
    auto direction = sample_direction(reader, context, directory, ref);
    result.eye_basis = std::move(direction.eye_basis);
    result.aim_punch = std::move(direction.aim_punch);
    result.view_offset = vector3(fields, modelentity::m_vecViewOffset);
    result.location = fields.text(pawn::m_szLastPlaceName);
    result.spotted = boolean(fields, pawn::m_entitySpottedState + offsets::schema::EntitySpottedState_t::m_bSpotted);
    result.spotted_by = fields.get<std::array<std::uint32_t, 2>>(
        pawn::m_entitySpottedState + offsets::schema::EntitySpottedState_t::m_bSpottedByMask);
    return result;
}
} // namespace
memory::Fields player_fields(memory::Address address) {
    namespace controller = offsets::schema::CCSPlayerController;
    namespace base = offsets::schema::CBasePlayerController;
    using memory::field;
    return memory::Fields(address, {{base::m_iszPlayerName, 128},
                                    field<memory::Address>(controller::m_sSanitizedPlayerName),
                                    field<std::uint64_t>(base::m_steamID),
                                    field<std::int32_t>(base::m_iConnected),
                                    field<std::uint8_t>(base::m_bIsLocalPlayerController),
                                    field<std::uint8_t>(offsets::schema::C_BaseEntity::m_iTeamNum),
                                    field<std::int32_t>(controller::m_iPing),
                                    field<std::int32_t>(controller::m_iScore),
                                    field<std::int32_t>(controller::m_iPawnBotDifficulty),
                                    field<std::uint8_t>(controller::m_bPawnIsAlive),
                                    field<std::uint32_t>(controller::m_iPawnHealth),
                                    field<std::uint32_t>(controller::m_iPawnArmor),
                                    field<std::uint32_t>(controller::m_hPlayerPawn),
                                    field<std::uint32_t>(controller::m_hObserverPawn),
                                    field<memory::Address>(controller::m_pInGameMoneyServices)});
}
model::Player sample_player(memory::IReadChannel& reader, const Context& context, const Directory& directory,
                            const EntitySample& sample, const EntitySample* body) {
    namespace controller = offsets::schema::CCSPlayerController;
    namespace base = offsets::schema::CBasePlayerController;
    const auto& ref = sample.ref;
    const auto& fields = sample.fields;
    model::Player result;
    result.controller.handle = ref.handle;
    result.name = fields.text(base::m_iszPlayerName);
    result.steam_id = fields.get<std::uint64_t>(base::m_steamID);
    result.connection_state = fields.get<std::int32_t>(base::m_iConnected);
    result.local = boolean(fields, base::m_bIsLocalPlayerController);
    result.team = fields.get<std::uint8_t>(offsets::schema::C_BaseEntity::m_iTeamNum);
    result.ping = fields.get<std::int32_t>(controller::m_iPing);
    result.score = fields.get<std::int32_t>(controller::m_iScore);
    result.bot_difficulty = fields.get<std::int32_t>(controller::m_iPawnBotDifficulty);
    result.controller_reports_alive = boolean(fields, controller::m_bPawnIsAlive);
    result.controller_reported_health = fields.get<std::uint32_t>(controller::m_iPawnHealth);
    result.controller_reported_armor = fields.get<std::uint32_t>(controller::m_iPawnArmor);
    if (const auto observer = fields.get<std::uint32_t>(controller::m_hObserverPawn);
        observer && *observer != UINT32_MAX)
        result.observer_pawn = model::EntityId{*observer};
    const auto pawn = fields.get<std::uint32_t>(controller::m_hPlayerPawn);
    if (pawn && *pawn != UINT32_MAX) {
        result.player_pawn = model::EntityId{*pawn};
        if (body)
            result.body = sample_body(reader, context, directory, *body);
    }
    return result;
}
} // namespace cs2::game
