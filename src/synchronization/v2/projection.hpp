#pragma once
#include "synchronization/v2/wire.hpp"
#include <array>

namespace cs2::model {
struct Snapshot;
} // namespace cs2::model

namespace cs2::synchronization::v2 {
using InstanceId = std::array<std::byte, 16>;
wire::Bytes encode_snapshot(const model::Snapshot&, const InstanceId& instance, const InstanceId& clock_domain);
} // namespace cs2::synchronization::v2
