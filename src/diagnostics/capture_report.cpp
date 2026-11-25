#include "diagnostics/capture_report.hpp"
#include "diagnostics/events.hpp"
#include <ostream>

namespace cs2::diagnostics {
void report_capture(std::ostream& log, const model::Snapshot& snapshot, std::uint64_t missed) {
    std::size_t bodies = 0, bone_points = 0;
    for (const auto& player : snapshot.players)
        if (player.body) {
            ++bodies;
            if (player.body->skeleton)
                bone_points += player.body->skeleton->points.size();
        }
    const double elapsed_ms = 1000.0 *
                              static_cast<double>(snapshot.interval.finished_qpc - snapshot.interval.started_qpc) /
                              snapshot.qpc_frequency;
    event(log, EventId::CaptureStats) << " seq=" << snapshot.sequence << " target_hz=" << snapshot.capture_target_hz
        << " slots=" << snapshot.coverage.slots << " players=" << snapshot.players.size() << " bodies=" << bodies
        << " weapons=" << snapshot.weapons.size() << " bombs=" << snapshot.bombs.size()
        << " projectiles=" << snapshot.projectiles.size() << " bones=" << bone_points
        << " local=" << snapshot.local.pawn.has_value() << " read_calls=" << snapshot.reads.calls
        << " read_bytes=" << snapshot.reads.requested << " read_failures=" << snapshot.reads.failures
        << " hc_single=" << snapshot.reads.hypercall_single << " hc_batch=" << snapshot.reads.hypercall_batch
        << " batch_items=" << snapshot.reads.batch_items << " batch_errors=" << snapshot.reads.batch_errors
        << " hc_dependent=" << snapshot.reads.hypercall_dependent
        << " dependent_items=" << snapshot.reads.dependent_items << " item_splits=" << snapshot.reads.batch_item_splits
        << " byte_splits=" << snapshot.reads.batch_byte_splits << " capture_ms=" << elapsed_ms << " missed=" << missed
        << '\n';
}
} // namespace cs2::diagnostics
