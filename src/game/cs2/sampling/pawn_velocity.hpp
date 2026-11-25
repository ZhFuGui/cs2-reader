#pragma once
#include "memory/transfer_result.hpp"
#include "model/accuracy.hpp"
#include <optional>
#include <span>

namespace cs2::memory {
class IReadChannel;
} // namespace cs2::memory

namespace cs2::game {
struct Context;
class Directory;
struct EntityRef;

// 本轮读取的完整句柄与来源，借用本轮目录中的稳定条目。
struct LocalAccuracyTarget {
    const EntityRef& controller;
    const EntityRef& pawn;
    const EntityRef& weapon;
    memory::Address weapon_services;
};
std::optional<LocalAccuracyTarget> sample_local_accuracy_target(memory::IReadChannel&, const Context&,
                                                                const Directory&);
// 与速度一起批读 owner。status 为 Valid 时，速度也能用于 Inaccuracy。
struct PawnVelocity {
    explicit PawnVelocity(const EntityRef& ref) : pawn(ref) {}
    const EntityRef& pawn;
    std::optional<model::Vec3> value;
    std::optional<model::EntityId> owner;
    model::AccuracyStatus status = model::AccuracyStatus::ReadFailed;
    std::optional<model::EntityId> reconstructed_for;
};
void sample_pawn_velocities(memory::IReadChannel&, const Context&, const Directory&, std::span<PawnVelocity>,
                            const LocalAccuracyTarget*);
// samples 按目录 slot index 排序，命中后仍比较完整句柄。
const PawnVelocity* find_pawn_velocity(std::span<const PawnVelocity> samples, std::uint32_t handle);
} // namespace cs2::game
