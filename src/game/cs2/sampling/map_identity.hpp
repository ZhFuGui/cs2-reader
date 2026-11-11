#pragma once
#include "game/cs2/session/context.hpp"
#include "model/world.hpp"

namespace cs2::game {
// 仅在采集通道中使用；原始地址属于内部来源证明，不进入消费协议。
struct MapObservation {
    model::MapStatus status = model::MapStatus::Unknown;
    memory::Address network = 0, name_pointer = 0, path_pointer = 0;
    std::int32_t sign_on = 0;
    std::uint8_t background = 0;
    std::optional<std::string> name, resource_path;
    bool operator==(const MapObservation&) const = default;
};
class MapIdentitySampler {
  public:
    MapObservation begin(memory::IReadChannel&, const Context&) const;
    model::MapIdentity finish(memory::IReadChannel&, const Context&, const MapObservation&);
    void invalidate() noexcept { previous_.reset(); }

  private:
    std::optional<MapObservation> previous_;
    std::uint64_t generation_ = 0;
};
} // namespace cs2::game
