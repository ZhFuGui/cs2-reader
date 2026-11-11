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
model::EntityState sample_common(memory::IReadChannel&, const EntityRef&);
model::EntityState sample_common(memory::IReadChannel&, const EntityRef&, const memory::Fields&);
} // namespace cs2::game
