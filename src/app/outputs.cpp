#include "app/outputs.hpp"
#include "diagnostics/snapshot_sink.hpp"
#include "synchronization/v1/service.hpp"
#include <ostream>

namespace cs2::app {
Outputs::Outputs(const Options& options, const capture::IClock& clock, capture::FrequencyControl& frequency) {
    if (options.service)
        service_ = std::make_unique<synchronization::v1::SubscriptionService>(
            synchronization::v1::ServiceOptions{options.port, options.maximum_clients, options.hz, options.bones,
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
void Outputs::unavailable(model::Availability value, std::string reason, std::optional<std::uint64_t> epoch) {
    if (service_)
        service_->unavailable(value, std::move(reason), epoch);
}
void Outputs::report(std::ostream& log) const {
    if (service_) {
        const auto stats = service_->stats();
        log << "tcp_clients=" << stats.connected << " encoded=" << stats.encoded << " sent=" << stats.sent
            << " pending_coalesced=" << stats.pending_coalesced << " encoder_coalesced=" << stats.encoder_coalesced;
        if (!stats.error.empty())
            log << " service_error=" << stats.error;
        log << '\n';
    }
    if (sink_)
        if (const auto error = sink_->error(); !error.empty())
            log << error << '\n';
}
std::optional<std::uint64_t> Outputs::coalesced() const {
    return sink_ ? std::optional(sink_->coalesced()) : std::nullopt;
}
} // namespace cs2::app
