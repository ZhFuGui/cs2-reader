#pragma once
#include "capture/source.hpp"
#include "capture/worker_pool.hpp"
#include "game/cs2/entity/directory.hpp"
#include "game/cs2/sampling/map_identity.hpp"
namespace cs2::game {
class Collector final : public capture::ISource {
  public:
    Collector(const memory::ISession&, Context, const capture::IClock&, std::size_t workers, bool bones);
    std::uint32_t build() const noexcept override { return context_.build; }
    model::Snapshot capture(std::uint64_t sequence, std::uint64_t epoch) override;
    void invalidate() noexcept override { map_.invalidate(); }

  private:
    const memory::ISession& session_;
    const Context context_;
    const capture::IClock& clock_;
    const bool bones_;
    std::unique_ptr<memory::IReadChannel> source_;
    memory::MeteredChannel reader_;
    capture::WorkerPool pool_;
    MapIdentitySampler map_;
};
} // namespace cs2::game
