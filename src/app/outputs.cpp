#include "app/outputs.hpp"
#include "diagnostics/events.hpp"
#include "diagnostics/snapshot_sink.hpp"
#include "synchronization/v2/service.hpp"
#include <ostream>

namespace cs2::app {
Outputs::Outputs(const Options& options, const capture::IClock& clock, capture::FrequencyControl& frequency) {
    if (options.service)
        service_ = std::make_unique<synchronization::v2::SubscriptionService>(
            synchronization::v2::ServiceOptions{options.port, options.maximum_clients, options.hz, options.bones,
                                                options.bind_address},
            clock, &frequency);
    if (!options.output.empty())
        sink_ = std::make_unique<diagnostics::SnapshotSink>(options.output);
}
Outputs::~Outputs() = default;
void Outputs::submit(std::shared_ptr<const model::Snapshot> snapshot) {
    if (service_)
        service_->submit(snapshot);
    if (sink_)
        sink_->submit(std::move(snapshot));
}
void Outputs::unavailable(model::Availability value, model::ReasonCode reason, std::optional<std::uint64_t> epoch) {
    if (service_)
        service_->unavailable(value, reason, epoch);
}
void Outputs::report(std::ostream& log) {
    if (service_) {
        const auto stats = service_->stats();
        diagnostics::event(log, diagnostics::EventId::SubscriptionStats)
            << " tcp_clients=" << stats.connected << " encoded=" << stats.encoded << " sent=" << stats.sent
            << " pending_coalesced=" << stats.pending_coalesced << " encoder_coalesced=" << stats.encoder_coalesced
            << '\n';
        const auto report_change = [&](const auto& fault, auto& previous) {
            if (fault && previous != fault)
                diagnostics::report_fault(log, *fault);
            previous = fault;
        };
        report_change(stats.encoding_fault, last_encoding_fault_);
        report_change(stats.network_fault, last_network_fault_);
    }
    if (sink_ && !reported_export_fault_ && sink_->failed()) {
        diagnostics::report_fault(log, {model::ReasonCode::SnapshotExportFailed});
        reported_export_fault_ = true;
    }
}
std::optional<std::uint64_t> Outputs::coalesced() const {
    return sink_ ? std::optional(sink_->coalesced()) : std::nullopt;
}
} // namespace cs2::app
