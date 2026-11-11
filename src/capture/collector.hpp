#pragma once
#include "capture/clock.hpp"
#include "capture/worker_pool.hpp"
#include "game/cs2/entity/directory.hpp"
#include "game/cs2/sampling/map_identity.hpp"
namespace cs2::capture {
class Collector {
  public:
    Collector(const memory::ISession&, game::Context, const IClock&, std::size_t workers, bool bones);
    model::Snapshot capture(std::uint64_t sequence, std::uint64_t epoch);
    void invalidate_map() noexcept { map_.invalidate(); }

  private:
    const memory::ISession& session_;
    const game::Context context_;
    const IClock& clock_;
    const bool bones_;
    std::unique_ptr<memory::IReadChannel> source_;
    memory::MeteredChannel reader_;
    WorkerPool pool_;
    game::MapIdentitySampler map_;
};
} // namespace cs2::capture
