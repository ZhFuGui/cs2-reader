#include "synchronization/v2/projection.hpp"
#include "synchronization/v2/projection_detail.hpp"
#include "model/snapshot.hpp"
#include <utility>

namespace cs2::synchronization::v2 {
using wire::Writer;

wire::Bytes encode_snapshot(const model::Snapshot& v, const InstanceId& instance, const InstanceId& clock_domain) {
    if (!v.sequence || !v.epoch || !v.qpc_frequency || !v.interval.started_qpc ||
        v.interval.started_qpc > v.interval.read_finished_qpc || v.interval.read_finished_qpc > v.interval.finished_qpc)
        throw wire::Error("invalid capture identity or time interval");
    const auto& c = v.coverage;
    if (c.empty + c.invalid + c.unreadable + c.changed + c.unknown + c.classified != c.slots || !v.directory_verified)
        throw wire::Error("incomplete directory coverage");
    Writer w, identity, clock, interval, coverage;
    (void)identity.blob(1, instance);
    (void)identity.fixed64(2, v.epoch);
    (void)identity.fixed64(3, v.sequence);
    identity.uint32(4, v.process_id);
    identity.uint32(5, v.game_build);
    w.message(1, identity);
    (void)clock.blob(1, clock_domain);
    (void)clock.fixed64(2, v.qpc_frequency);
    w.message(2, clock);
    (void)interval.fixed64(1, v.interval.started_qpc);
    (void)interval.fixed64(2, v.interval.read_finished_qpc);
    (void)interval.fixed64(3, v.interval.finished_qpc);
    w.message(3, interval);
    (void)coverage.fixed64(1, c.slots);
    (void)coverage.fixed64(2, c.empty);
    (void)coverage.fixed64(3, c.invalid);
    (void)coverage.fixed64(4, c.unreadable);
    (void)coverage.fixed64(5, c.changed);
    (void)coverage.fixed64(6, c.unknown);
    (void)coverage.fixed64(7, c.classified);
    coverage.sint32(8, c.reported_highest_index);
    coverage.uint32(9, c.allocated_chunks);
    coverage.boolean(10, v.directory_verified);
    w.message(4, coverage);
    for (const auto& value : v.players)
        w.message(5, projection::player(value));
    for (const auto& value : v.weapons)
        w.message(6, projection::weapon(value));
    for (const auto& value : v.bombs)
        w.message(7, projection::bomb(value));
    for (const auto& value : v.projectiles)
        w.message(8, projection::projectile(value));
    for (const auto& value : v.world_objects)
        w.message(9, projection::entity(value));
    w.message(10, projection::local(v.local));
    w.message(11, projection::world(v.world));
    return std::move(w.bytes);
}
} // namespace cs2::synchronization::v2
