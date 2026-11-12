#pragma once
#include "game/cs2/entity/directory.hpp"
#include "memory/block_reader.hpp"
namespace cs2::game {
std::optional<bool> boolean(const memory::Fields&, std::size_t);
std::optional<float> real(const memory::Fields&, std::size_t);
std::optional<model::Vec3> vector3(const memory::Fields&, std::size_t);
std::optional<model::Angles> angles(const memory::Fields&, std::size_t);
// 同一对象开读时，把公共字段与类型字段合并；不用于跨采样阶段复用。
std::span<const memory::Field> entity_fields() noexcept;
// 由批次拥有字段缓冲及身份核验生命周期；类型采样器只借用本轮数据。
struct EntitySample {
    const EntityRef& ref;
    const memory::Fields& fields;
    model::Transform transform;
};
struct TransformRead {
    const EntityRef& ref;
    const memory::Fields& fields;
    model::Transform& result;
};
void sample_transforms(memory::IReadChannel&, std::span<const TransformRead>);
model::EntityState project_common(const EntitySample&);
model::EntityState sample_common(memory::IReadChannel&, const EntityRef&);
model::EntityState sample_common(memory::IReadChannel&, const EntityRef&, const memory::Fields&);
} // namespace cs2::game
