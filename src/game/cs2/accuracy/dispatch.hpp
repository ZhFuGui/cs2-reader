#pragma once
#include "game/cs2/accuracy/profile.hpp"
#include "game/cs2/session/context.hpp"
#include "memory/observation.hpp"
#include "model/accuracy.hpp"
#include <array>

namespace cs2::game::accuracy_dispatch {
using Status = model::AccuracyStatus;
struct Failure {
    Status status;
};
inline bool supports_accuracy(const Context& context) {
    return context.build == accuracy_profile::build && context.client.timestamp == accuracy_profile::timestamp &&
           context.client.size == accuracy_profile::image_size;
}
inline void verify_entry(const Context& context, memory::IReadChannel& reader, memory::Observation& observation,
                         const accuracy_profile::Entry& expected) {
    const auto bytes = context.static_data.value<std::array<unsigned char, 16>>(
        reader, context.client.base + expected.rva, &observation);
    if (!bytes)
        throw Failure{Status::ReadFailed};
    if (*bytes != expected.bytes)
        throw Failure{Status::UnsupportedDispatch};
}
} // namespace cs2::game::accuracy_dispatch
