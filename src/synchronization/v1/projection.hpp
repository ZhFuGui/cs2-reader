#pragma once
#include "model/snapshot.hpp"
#include "synchronization/v1/wire.hpp"
#include <array>

namespace cs2::synchronization::v1 {
using InstanceId = std::array<std::byte, 16>;
wire::Bytes encode_snapshot(const model::Snapshot&, const InstanceId& instance, const InstanceId& clock_domain);
} // namespace cs2::synchronization::v1
