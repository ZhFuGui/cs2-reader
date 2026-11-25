#include "game/cs2/capture/collector.hpp"
#include "game/cs2/accuracy/profile.hpp"
#include "game/cs2/sampling/world.hpp"
#include "memory/read_error.hpp"
#include <array>
#include <stdexcept>
#include <utility>

namespace cs2::game {
namespace {
memory::IReadChannel& require_reader(const std::unique_ptr<memory::IReadChannel>& source) {
    if (!source)
        throw std::logic_error("Backend did not provide a capture reader");
    return *source;
}
Context prepare_context(memory::IReadChannel& reader, Context context) {
    namespace p = accuracy_profile;
    constexpr std::array entries{p::inaccuracy,         p::spread,          p::maximum_speed,   p::c4_speed,
                                 p::movement,           p::ground,          p::owner,           p::velocity,
                                 p::velocity_recompute, p::velocity_parent, p::velocity_source, p::velocity_cache};
    std::array<StaticData::Range, entries.size()> preload{};
    for (std::size_t i = 0; i < entries.size(); ++i)
        preload[i] = {context.client.base + entries[i].rva, entries[i].bytes.size()};
    const std::array modules{context.client, context.engine};
    context.static_data.initialize(reader, modules, preload);

    return context;
}
} // namespace

Collector::Collector(const memory::ISession& session, Context context, const capture::IClock& clock,
                     std::size_t workers, bool bones)
    : process_id_(session.identity().pid), clock_(clock), source_(session.open_reader()),
      reader_(require_reader(source_)), context_(prepare_context(reader_, std::move(context))),
      entities_(session, context_, clock, workers, bones) {}

model::Snapshot Collector::capture(std::uint64_t sequence, std::uint64_t epoch) {
    (void)reader_.take_metrics();
    (void)entities_.take_metrics();

    model::Snapshot result;
    result.sequence = sequence;
    result.epoch = epoch;
    result.qpc_frequency = clock_.frequency();
    result.process_id = process_id_;
    result.game_build = context_.build;
    result.interval.started_qpc = clock_.now(); // 记录在本轮首次读取之前。

    const auto map_opening = map_.begin(reader_, context_);
    const auto directory = entities_.discover(reader_, context_);
    auto samples = entities_.sample(reader_, context_, directory);
    result.world = sample_world(reader_, context_);
    if (!directory.verify_root(reader_, context_))
        throw memory::ReadUnavailable("Entity directory invalidated before capture submission");
    result.directory_verified = true;
    result.world.map_identity = map_.finish(reader_, context_, map_opening);
    result.interval.read_finished_qpc = clock_.now();
    result.coverage = directory.coverage();
    result.reads = reader_.take_metrics();
    result.reads += entities_.take_metrics();
    std::move(samples).append_to(result);
    result.interval.finished_qpc = clock_.now();
    return result;
}
} // namespace cs2::game
