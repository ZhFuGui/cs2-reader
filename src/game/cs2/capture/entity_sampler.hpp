#pragma once
#include "capture/worker_pool.hpp"
#include "game/cs2/sampling/batch.hpp"
#include "game/cs2/session/context.hpp"
#include "model/local.hpp"

namespace cs2::model {
struct Snapshot;
} // namespace cs2::model

namespace cs2::game {
struct EntitySamples {
    model::Local local;
    std::vector<EntityValue> values;
    void append_to(model::Snapshot&) &&;
};

// 每次绑定拥有独立工作通道和各 worker 的惰性缓存。
// 目录与属性缓冲只在当前采集内生存，由调用者在封帧前核验目录根。
class EntitySampler final {
  public:
    EntitySampler(const memory::ISession&, const Context&, const capture::IClock&, std::size_t workers, bool bones);
    Directory discover(memory::IReadChannel&, const Context&);
    EntitySamples sample(memory::IReadChannel&, const Context&, const Directory&);
    diagnostics::ReadMetrics take_metrics() { return pool_.take_metrics(); }

  private:
    std::vector<Context> worker_contexts_;
    capture::WorkerPool pool_;
    const capture::IClock& clock_;
    const bool bones_;
};
} // namespace cs2::game
