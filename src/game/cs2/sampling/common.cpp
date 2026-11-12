#include "game/cs2/sampling/common.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "memory/read_plan.hpp"
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
        field<memory::Address>(m_pGameSceneNode), field<std::int32_t>(m_iMaxHealth),   field<std::int32_t>(m_iHealth),
        field<std::uint8_t>(m_lifeState),         field<std::uint8_t>(m_iTeamNum),     field<std::uint32_t>(m_fFlags),
        field<model::Vec3>(m_vecAbsVelocity),     field<std::uint32_t>(m_hOwnerEntity)};
    return fields;
}
model::EntityState sample_common(memory::IReadChannel& reader, const EntityRef& ref) {
    const memory::Fields fields(reader, ref.address, entity_fields());
    return sample_common(reader, ref, fields);
}
model::EntityState sample_common(memory::IReadChannel& reader, const EntityRef& ref, const memory::Fields& fields) {
    model::Transform transform;
    const TransformRead request{ref, fields, transform};
    sample_transforms(reader, std::span(&request, 1));
    return project_common({ref, fields, std::move(transform)});
}
model::EntityState project_common(const EntitySample& sample) {
    using namespace offsets::schema::C_BaseEntity;
    const auto& ref = sample.ref;
    const auto& fields = sample.fields;
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
    result.transform = sample.transform;
    return result;
}
void sample_transforms(memory::IReadChannel& reader, std::span<const TransformRead> requests) {
    using namespace offsets::schema::CGameSceneNode;
    using memory::field;
    struct Scene {
        const TransformRead* request;
        memory::Address node, closing{};
        memory::Fields fields;
        memory::ReadPlan::Ticket ticket{};
    };
    std::vector<Scene> scenes;
    scenes.reserve(requests.size());
    for (const auto& request : requests) {
        request.result = {};
        const auto node = request.fields.get<memory::Address>(offsets::schema::C_BaseEntity::m_pGameSceneNode);
        if (node && memory::user_range(*node))
            scenes.push_back(
                {&request,
                 *node,
                 {},
                 memory::Fields(*node, {field<memory::Address>(m_pOwner), field<model::Vec3>(m_vecAbsOrigin),
                                        field<model::Angles>(m_angAbsRotation), field<float>(m_flAbsScale),
                                        field<std::uint8_t>(m_bDormant)})});
    }
    std::vector<memory::Fields*> fields;
    for (auto& scene : scenes)
        fields.push_back(&scene.fields);
    memory::Fields::read_many(reader, fields);
    memory::ReadPlan closing(scenes.size());
    for (auto& scene : scenes)
        scene.ticket =
            closing.add(scene.request->ref.address + offsets::schema::C_BaseEntity::m_pGameSceneNode, scene.closing);
    closing.execute(reader);
    for (const auto& scene : scenes) {
        if (!closing.complete(scene.ticket) || scene.closing != scene.node ||
            scene.fields.get<memory::Address>(m_pOwner) != std::optional(scene.request->ref.address))
            continue;
        auto& transform = scene.request->result;
        transform.position = vector3(scene.fields, m_vecAbsOrigin);
        transform.rotation = angles(scene.fields, m_angAbsRotation);
        transform.scale = real(scene.fields, m_flAbsScale);
        transform.dormant = boolean(scene.fields, m_bDormant);
    }
}
} // namespace cs2::game
