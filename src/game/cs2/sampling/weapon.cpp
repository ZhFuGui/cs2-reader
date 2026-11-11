#include "game/cs2/sampling/weapon.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "game/cs2/sampling/common.hpp"
#include "game/cs2/sampling/accuracy.hpp"
namespace cs2::game {
std::optional<model::Weapon> sample_weapon(memory::IReadChannel& reader, const Context& context,
                                           const Directory& directory, const EntityRef& ref,
                                           const capture::IClock& clock, const AccuracyRules* rules) {
    if (ref.kind != EntityKind::Weapon || !directory.verify(reader, ref))
        return std::nullopt;
    namespace econ = offsets::schema::C_EconEntity;
    namespace item = offsets::schema::C_EconItemView;
    namespace ammo = offsets::schema::C_BasePlayerWeapon;
    namespace weapon = offsets::schema::C_CSWeaponBase;
    constexpr auto item_offset = econ::m_AttributeManager + offsets::schema::C_AttributeContainer::m_Item;
    using memory::field;
    const memory::Fields fields(reader, ref.address, entity_fields(),
                                {field<std::uint16_t>(item_offset + item::m_iItemDefinitionIndex),
                                 {item_offset + item::m_szCustomName, 161},
                                 field<std::int32_t>(econ::m_nFallbackPaintKit),
                                 field<float>(econ::m_flFallbackWear),
                                 field<std::int32_t>(econ::m_nFallbackStatTrak),
                                 field<std::int32_t>(ammo::m_iClip1),
                                 field<std::int32_t>(ammo::m_iClip2),
                                 field<std::int32_t>(ammo::m_pReserveAmmo),
                                 field<float>(weapon::m_fAccuracyPenalty),
                                 field<float>(weapon::m_flRecoilIndex),
                                 field<std::uint8_t>(weapon::m_bInReload),
                                 field<std::uint8_t>(weapon::m_bSilencerOn),
                                 field<std::uint8_t>(weapon::m_bBurstMode)});
    model::Weapon result;
    result.entity = sample_common(reader, ref, fields);
    result.definition_index = fields.get<std::uint16_t>(item_offset + item::m_iItemDefinitionIndex);
    result.custom_name = fields.text(item_offset + item::m_szCustomName);
    result.clip = fields.get<std::int32_t>(ammo::m_iClip1);
    result.secondary_clip = fields.get<std::int32_t>(ammo::m_iClip2);
    result.reserve_ammo = fields.get<std::int32_t>(ammo::m_pReserveAmmo);
    result.paint_kit = fields.get<std::int32_t>(econ::m_nFallbackPaintKit);
    result.wear = real(fields, econ::m_flFallbackWear);
    result.stattrak = fields.get<std::int32_t>(econ::m_nFallbackStatTrak);
    result.accuracy_penalty = real(fields, weapon::m_fAccuracyPenalty);
    result.recoil_index = real(fields, weapon::m_flRecoilIndex);
    result.reloading = boolean(fields, weapon::m_bInReload);
    result.silenced = boolean(fields, weapon::m_bSilencerOn);
    result.burst_mode = boolean(fields, weapon::m_bBurstMode);
    result.accuracy_evaluation = sample_weapon_accuracy(reader, context, directory, ref, clock, rules);
    if (!directory.verify(reader, ref))
        return std::nullopt;
    return result;
}
} // namespace cs2::game
