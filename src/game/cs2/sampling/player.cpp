#include "game/cs2/sampling/player.hpp"
#include "game/cs2/entity/layout.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "game/cs2/sampling/common.hpp"
#include "game/cs2/sampling/direction.hpp"
#include "game/cs2/sampling/skeleton.hpp"
#include "game/cs2/sampling/weapon.hpp"
#include <algorithm>

namespace cs2::game {
namespace {
std::optional<model::PlayerBody> sample_body(memory::IReadChannel& reader, const Context& context,
                                             const Directory& directory, const EntityRef& ref, bool bones) {
    if (ref.kind != EntityKind::PlayerPawn || !directory.verify(reader, ref))
        return std::nullopt;
    namespace base = offsets::schema::C_BasePlayerPawn;
    namespace pawn = offsets::schema::C_CSPlayerPawn;
    namespace pawnbase = offsets::schema::C_CSPlayerPawnBase;
    namespace modelentity = offsets::schema::C_BaseModelEntity;
    using memory::field;
    const memory::Fields fields(
        reader, ref.address, entity_fields(),
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
    model::PlayerBody result;
    result.entity = sample_common(reader, ref, fields);
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
    if (const auto services = fields.get<memory::Address>(base::m_pItemServices);
        services && memory::user_range(*services)) {
        namespace item = offsets::schema::CCSPlayer_ItemServices;
        const memory::Fields values(
            reader, *services, {field<std::uint8_t>(item::m_bHasHelmet), field<std::uint8_t>(item::m_bHasDefuser)});
        if (memory::read_value<memory::Address>(reader, ref.address + base::m_pItemServices) == services) {
            result.helmet = boolean(values, item::m_bHasHelmet);
            result.defuser = boolean(values, item::m_bHasDefuser);
        }
    }
    if (const auto services = fields.get<memory::Address>(base::m_pWeaponServices);
        services && memory::user_range(*services)) {
        namespace weapon = offsets::schema::CPlayer_WeaponServices;
        struct VectorHeader {
            std::int32_t count;
            std::uint32_t reserved;
            memory::Address data;
        };
        const memory::Fields values(
            reader, *services,
            {field<std::uint32_t>(weapon::m_hActiveWeapon), field<VectorHeader>(weapon::m_hMyWeapons)});
        if (const auto active = values.get<std::uint32_t>(weapon::m_hActiveWeapon); active && *active != UINT32_MAX) {
            result.active_weapon = model::EntityId{*active};
        }
        const auto vector = values.get<VectorHeader>(weapon::m_hMyWeapons);
        // 以句柄索引空间验证容器格式，不按武器数量随意截断。
        if (vector && vector->count >= 0 &&
            static_cast<std::uint32_t>(vector->count) <= layout::entity_index_mask + 1 &&
            (!vector->count ||
             memory::user_range(vector->data, static_cast<std::size_t>(vector->count) * sizeof(std::uint32_t)))) {
            memory::Block handles;
            handles.read(reader, vector->data, static_cast<std::size_t>(vector->count) * sizeof(std::uint32_t));
            for (std::int32_t i = 0; i < vector->count; ++i)
                if (const auto handle = handles.get<std::uint32_t>(i * sizeof(std::uint32_t));
                    handle && *handle != UINT32_MAX)
                    result.equipped_weapons.push_back({*handle});
            const auto close = memory::read_value<VectorHeader>(reader, *services + weapon::m_hMyWeapons);
            if (!close || close->count != vector->count || close->data != vector->data)
                result.equipped_weapons.clear();
        }
        if (memory::read_value<memory::Address>(reader, ref.address + base::m_pWeaponServices) != services) {
            result.active_weapon.reset();
            result.weapon.reset();
            result.equipped_weapons.clear();
        }
    }
    if (bones)
        result.skeleton = sample_skeleton(
            reader, ref.address, fields.get<memory::Address>(offsets::schema::C_BaseEntity::m_pGameSceneNode));
    if (!directory.verify(reader, ref))
        return std::nullopt;
    return result;
}
} // namespace
std::optional<model::Player> sample_player(memory::IReadChannel& reader, const Context& context,
                                           const Directory& directory, const EntityRef& ref, bool bones) {
    if (ref.kind != EntityKind::Controller || !directory.verify(reader, ref))
        return std::nullopt;
    namespace controller = offsets::schema::CCSPlayerController;
    namespace base = offsets::schema::CBasePlayerController;
    using memory::field;
    const memory::Fields fields(reader, ref.address,
                                {{base::m_iszPlayerName, 128},
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
    if (const auto pointer = fields.get<memory::Address>(controller::m_sSanitizedPlayerName);
        pointer && memory::user_range(*pointer)) {
        result.sanitized_name = memory::read_string(reader, *pointer, 128);
        if (memory::read_value<memory::Address>(reader, ref.address + controller::m_sSanitizedPlayerName) != pointer)
            result.sanitized_name.reset();
    }
    if (const auto money = fields.get<memory::Address>(controller::m_pInGameMoneyServices);
        money && memory::user_range(*money)) {
        result.money = memory::read_value<std::int32_t>(
            reader, *money + offsets::schema::CCSPlayerController_InGameMoneyServices::m_iAccount);
        if (memory::read_value<memory::Address>(reader, ref.address + controller::m_pInGameMoneyServices) != money)
            result.money.reset();
    }
    if (const auto observer = fields.get<std::uint32_t>(controller::m_hObserverPawn);
        observer && *observer != UINT32_MAX)
        result.observer_pawn = model::EntityId{*observer};
    const auto pawn = fields.get<std::uint32_t>(controller::m_hPlayerPawn);
    if (pawn && *pawn != UINT32_MAX) {
        result.player_pawn = model::EntityId{*pawn};
        if (const auto* entity = directory.resolve(*pawn))
            result.body = sample_body(reader, context, directory, *entity, bones);
        if (memory::read_value<std::uint32_t>(reader, ref.address + controller::m_hPlayerPawn) != pawn) {
            result.player_pawn.reset();
            result.body.reset();
        }
    }
    if (!directory.verify(reader, ref))
        return std::nullopt;
    return result;
}
} // namespace cs2::game
