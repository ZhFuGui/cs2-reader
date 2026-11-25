#pragma once
#include "capture/source.hpp"
#include "game/cs2/capture/entity_sampler.hpp"
#include "game/cs2/sampling/map_identity.hpp"
#include "memory/metered_channel.hpp"

namespace cs2::game {
class Collector final : public capture::ISource {
  public:
    Collector(const memory::ISession&, Context, const capture::IClock&, std::size_t workers, bool bones);
    std::uint32_t build() const noexcept override { return context_.build; }
    model::Snapshot capture(std::uint64_t sequence, std::uint64_t epoch) override;
    void invalidate() noexcept override { map_.invalidate(); }

  private:
    const std::uint32_t process_id_;
    const capture::IClock& clock_;
    std::unique_ptr<memory::IReadChannel> source_;
    memory::MeteredChannel reader_;
    Context context_;
    EntitySampler entities_;
    MapIdentitySampler map_;
};
} // namespace cs2::game
