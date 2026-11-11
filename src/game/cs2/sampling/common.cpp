#include "game/cs2/sampling/common.hpp"
#include "game/cs2/offsets/catalog.hpp"
namespace cs2::game {
std::optional<bool> boolean(const memory::Fields& fields, std::size_t offset) {
    const auto value = fields.get<std::uint8_t>(offset);
    if (!value || *value > 1)
        return std::nullopt;
    return *value != 0;
}
std::optional<float> real(const memory::Fields& fields, std::size_t offset) {
    const auto value = fields.get<float>(offset);
    return value && std::isfinite(*value) ? value : std::nullopt;
}
std::optional<model::Vec3> vector3(const memory::Fields& fields, std::size_t offset) {
    const auto value = fields.get<model::Vec3>(offset);
    return value && model::finite(*value) ? value : std::nullopt;
}
std::optional<model::Angles> angles(const memory::Fields& fields, std::size_t offset) {
    const auto value = fields.get<model::Angles>(offset);
    return value && model::finite(*value) ? value : std::nullopt;
}
std::span<const memory::Field> entity_fields() noexcept {
    using namespace offsets::schema::C_BaseEntity;
    using memory::field;
    static constexpr memory::Field fields[]{
        field<memory::Address>(m_pGameSceneNode), field<std::int32_t>(m_iMaxHealth),
        field<std::int32_t>(m_iHealth), field<std::uint8_t>(m_lifeState),
        field<std::uint8_t>(m_iTeamNum), field<std::uint32_t>(m_fFlags),
        field<model::Vec3>(m_vecAbsVelocity), field<std::uint32_t>(m_hOwnerEntity)};
    return fields;
}
model::EntityState sample_common(memory::IReadChannel& reader, const EntityRef& ref) {
    const memory::Fields fields(reader, ref.address, entity_fields());
    return sample_common(reader, ref, fields);
}
model::EntityState sample_common(memory::IReadChannel& reader, const EntityRef& ref, const memory::Fields& fields) {
    using namespace offsets::schema::C_BaseEntity;
    using memory::field;
    model::EntityState result;
    result.id.handle = ref.handle;
    result.designer_name = ref.designer_name;
    result.health = fields.get<std::int32_t>(m_iHealth);
    result.max_health = fields.get<std::int32_t>(m_iMaxHealth);
    result.team = fields.get<std::uint8_t>(m_iTeamNum);
    result.life_state = fields.get<std::uint8_t>(m_lifeState);
    result.flags = fields.get<std::uint32_t>(m_fFlags);
    result.world_velocity = vector3(fields, m_vecAbsVelocity);
    if (const auto owner = fields.get<std::uint32_t>(m_hOwnerEntity); owner && *owner != UINT32_MAX)
        result.owner = model::EntityId{*owner};
    const auto node = fields.get<memory::Address>(m_pGameSceneNode);
    if (node && memory::user_range(*node)) {
        using namespace offsets::schema::CGameSceneNode;
        const memory::Fields scene(reader, *node,
                                   {field<memory::Address>(m_pOwner), field<model::Vec3>(m_vecAbsOrigin),
                                    field<model::Angles>(m_angAbsRotation), field<float>(m_flAbsScale),
                                    field<std::uint8_t>(m_bDormant)});
        if (scene.get<memory::Address>(m_pOwner) == std::optional(ref.address)) {
            result.transform.position = vector3(scene, m_vecAbsOrigin);
            result.transform.rotation = angles(scene, m_angAbsRotation);
            result.transform.scale = real(scene, m_flAbsScale);
            result.transform.dormant = boolean(scene, m_bDormant);
        }
        if (memory::read_value<memory::Address>(reader, ref.address + m_pGameSceneNode) != node)
            result.transform = {};
    }
    return result;
}
} // namespace cs2::game
