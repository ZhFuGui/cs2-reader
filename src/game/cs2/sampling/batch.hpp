#pragma once
#include "capture/clock.hpp"
#include "game/cs2/entity/directory.hpp"
#include "game/cs2/sampling/common.hpp"
#include <span>
#include <variant>

namespace cs2::game {
class AccuracyRules;
using EntityValue =
    std::variant<std::monostate, model::Player, model::Weapon, model::Bomb, model::Projectile, model::EntityState>;
// 组内共享批量字段读取；深层求值按单对象重新领取，避免慢对象独占整组算力。
// 调用方在阶段间建立屏障，evaluate 可并行写不同下标，close 后才能交付结果。
// 仅保存本轮数据和证明，不保存执行它的 worker 或通道。
class EntityBatch {
  public:
    EntityBatch(std::span<const EntityRef* const>, std::span<EntityValue>);
    EntityBatch(const EntityBatch&) = delete;
    EntityBatch& operator=(const EntityBatch&) = delete;
    EntityBatch(EntityBatch&&) noexcept = default;
    EntityBatch& operator=(EntityBatch&&) noexcept = default;
    void prepare(memory::IReadChannel&, const Directory&);
    void evaluate(memory::IReadChannel&, const Context&, const Directory&, std::size_t index, const capture::IClock&,
                  const AccuracyRules*);
    void close(memory::IReadChannel&, bool bones);

  private:
    enum class Phase { Created, Preparing, Prepared, Closed };
    struct Prepared {
        const EntityRef& ref;
        memory::Fields fields;
        model::Transform transform;
        EntitySample sample() const { return {ref, fields, transform}; }
    };
    std::span<const EntityRef* const> refs_;
    std::span<EntityValue> values_;
    std::vector<std::optional<Prepared>> roots_, bodies_;
    Phase phase_ = Phase::Created;
};
} // namespace cs2::game
