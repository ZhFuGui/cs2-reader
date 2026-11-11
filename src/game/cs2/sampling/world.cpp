#include "game/cs2/sampling/world.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "game/cs2/sampling/common.hpp"
namespace cs2::game {
model::World sample_world(memory::IReadChannel& reader, const Context& context) {
    model::World result;
    const auto pointer =
        memory::read_value<memory::Address>(reader, context.client.base + offsets::client::dwGameRules);
    if (!pointer || !memory::user_range(*pointer))
        return result;
    using namespace offsets::schema::C_CSGameRules;
    using memory::field;
    const memory::Fields fields(reader, *pointer,
                                {field<std::uint8_t>(m_bFreezePeriod), field<std::uint8_t>(m_bWarmupPeriod),
                                 field<std::uint8_t>(m_bHasMatchStarted), field<std::uint8_t>(m_bBombPlanted),
                                 field<std::uint8_t>(m_bBombDropped), field<std::int32_t>(m_iRoundTime),
                                 field<std::int32_t>(m_totalRoundsPlayed), field<std::int32_t>(m_iRoundWinStatus),
                                 field<std::int32_t>(m_gamePhase), field<float>(m_fRoundStartTime),
                                 field<model::Vec3>(m_vMinimapMins), field<model::Vec3>(m_vMinimapMaxs)});
    result.freeze_period = boolean(fields, m_bFreezePeriod);
    result.warmup = boolean(fields, m_bWarmupPeriod);
    result.match_started = boolean(fields, m_bHasMatchStarted);
    result.bomb_planted = boolean(fields, m_bBombPlanted);
    result.bomb_dropped = boolean(fields, m_bBombDropped);
    result.round_time = fields.get<std::int32_t>(m_iRoundTime);
    result.rounds_played = fields.get<std::int32_t>(m_totalRoundsPlayed);
    result.round_win_status = fields.get<std::int32_t>(m_iRoundWinStatus);
    result.phase = fields.get<std::int32_t>(m_gamePhase);
    result.round_started_at = real(fields, m_fRoundStartTime);
    result.minimap_min = vector3(fields, m_vMinimapMins);
    result.minimap_max = vector3(fields, m_vMinimapMaxs);
    if (memory::read_value<memory::Address>(reader, context.client.base + offsets::client::dwGameRules) != pointer)
        return {};
    return result;
}
std::optional<model::Bomb> sample_bomb(memory::IReadChannel& reader, const Directory& directory, const EntityRef& ref) {
    if (ref.kind != EntityKind::Bomb || !directory.verify(reader, ref))
        return std::nullopt;
    using namespace offsets::schema::C_PlantedC4;
    using memory::field;
    const memory::Fields fields(reader, ref.address, entity_fields(),
                                {field<std::uint8_t>(m_bBombTicking), field<std::uint8_t>(m_bBombDefused),
                                 field<std::uint8_t>(m_bBeingDefused), field<std::uint8_t>(m_bHasExploded),
                                 field<std::int32_t>(m_nBombSite), field<float>(m_flC4Blow),
                                 field<float>(m_flTimerLength), field<float>(m_flDefuseCountDown),
                                 field<float>(m_flDefuseLength), field<std::uint32_t>(m_hBombDefuser)});
    model::Bomb result;
    result.entity = sample_common(reader, ref, fields);
    result.ticking = boolean(fields, m_bBombTicking);
    result.defused = boolean(fields, m_bBombDefused);
    result.being_defused = boolean(fields, m_bBeingDefused);
    result.exploded = boolean(fields, m_bHasExploded);
    result.site = fields.get<std::int32_t>(m_nBombSite);
    result.explode_at = real(fields, m_flC4Blow);
    result.timer_length = real(fields, m_flTimerLength);
    result.defuse_at = real(fields, m_flDefuseCountDown);
    result.defuse_length = real(fields, m_flDefuseLength);
    if (const auto defuser = fields.get<std::uint32_t>(m_hBombDefuser); defuser && *defuser != UINT32_MAX)
        result.defuser = model::EntityId{*defuser};
    if (!directory.verify(reader, ref))
        return std::nullopt;
    return result;
}
std::optional<model::Projectile> sample_projectile(memory::IReadChannel& reader, const Directory& directory,
                                                   const EntityRef& ref) {
    if (ref.kind != EntityKind::Projectile || !directory.verify(reader, ref))
        return std::nullopt;
    using namespace offsets::schema::C_BaseCSGrenadeProjectile;
    using memory::field;
    const memory::Fields fields(reader, ref.address, entity_fields(),
                                {field<model::Vec3>(m_vInitialPosition), field<model::Vec3>(m_vInitialVelocity),
                                 field<std::int32_t>(m_nBounces), field<std::int32_t>(m_nExplodeEffectTickBegin),
                                 field<std::uint8_t>(m_bExplodeEffectBegan), field<float>(m_flSpawnTime)});
    model::Projectile result;
    result.entity = sample_common(reader, ref, fields);
    result.initial_position = vector3(fields, m_vInitialPosition);
    result.initial_velocity = vector3(fields, m_vInitialVelocity);
    result.bounces = fields.get<std::int32_t>(m_nBounces);
    result.explode_tick = fields.get<std::int32_t>(m_nExplodeEffectTickBegin);
    result.exploded = boolean(fields, m_bExplodeEffectBegan);
    result.spawn_time = real(fields, m_flSpawnTime);
    if (ref.designer_name == "smokegrenade_projectile") {
        using namespace offsets::schema::C_SmokeGrenadeProjectile;
        const memory::Fields smoke(
            reader, ref.address,
            {field<std::int32_t>(m_nSmokeEffectTickBegin), field<std::uint8_t>(m_bDidSmokeEffect)});
        result.smoke_tick = smoke.get<std::int32_t>(m_nSmokeEffectTickBegin);
        result.smoke_started = boolean(smoke, m_bDidSmokeEffect);
    }
    if (!directory.verify(reader, ref))
        return std::nullopt;
    return result;
}
} // namespace cs2::game
