#pragma once
#include "model/recoil.hpp"

namespace cs2::memory {
class IReadChannel;
} // namespace cs2::memory

namespace cs2::game {
struct Context;
class Directory;
struct EntityRef;

std::optional<model::RecoilTimes> sample_recoil_times(memory::IReadChannel&, const Context&, const Directory&,
                                                      const EntityRef&);
std::optional<float> sample_view_punch_time(memory::IReadChannel&, const Context&, const EntityRef&,
                                            std::uint8_t predictable);
} // namespace cs2::game
