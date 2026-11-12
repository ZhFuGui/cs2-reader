#include "capture/collector.hpp"
#include "game/cs2/accuracy/rules.hpp"
#include "game/cs2/sampling/batch.hpp"
#include "game/cs2/sampling/local.hpp"
#include "game/cs2/sampling/world.hpp"
#include "memory/read_error.hpp"
#include <algorithm>
#include <stdexcept>
#include <variant>
namespace cs2::capture {
Collector::Collector(const memory::ISession& session, game::Context context, const IClock& clock, std::size_t workers,
                     bool bones)
    : session_(session), context_(std::move(context)), clock_(clock), bones_(bones), source_(session.open_reader()),
      reader_(*source_), pool_(session, workers) {}
model::Snapshot Collector::capture(std::uint64_t sequence, std::uint64_t epoch) {
    (void)reader_.take_metrics();
    (void)pool_.take_metrics();
    model::Snapshot result;
    result.sequence = sequence;
    result.epoch = epoch;
    result.qpc_frequency = clock_.frequency();
    result.process_id = session_.identity().pid;
    result.game_build = context_.build;
    result.interval.started_qpc = clock_.now(); // 必须先于本轮地图与目录的第一次读取。
    const auto map_opening = map_.begin(reader_, context_);
    game::Directory directory;
    directory.discover(reader_, context_);
    std::vector<std::size_t> identities;
    for (std::size_t i = 0; i < directory.slots.size(); ++i)
        if (directory.slots[i].state == game::SlotState::Pending)
            identities.push_back(i);
    // 只决定每个 worker 一次推进多少对象；最终循环仍覆盖全部任务。
    constexpr std::size_t classification_group = 16, sampling_group = 8;
    pool_.run_ranges(identities.size(), classification_group,
                     [&](memory::IReadChannel& channel, auto first, auto last) {
                         directory.classify_many(channel, std::span(identities).subspan(first, last - first));
                     });
    directory.resolve_player_aliases(reader_);
    std::vector<const game::EntityRef*> tasks;
    for (std::size_t i = 0; i < directory.slots.size(); ++i) {
        const auto& slot = directory.slots[i];
        if (slot.state == game::SlotState::Classified && slot.entity.kind != game::EntityKind::PlayerPawn &&
            slot.entity.kind != game::EntityKind::ObserverPawn)
            tasks.push_back(&slot.entity);
    }
    // 全槽位仍有覆盖终态；仅为实际属性任务分配大结果对象，不为空槽位构造 Player/Weapon 变体。
    std::vector<game::EntityValue> values(tasks.size());
    std::vector<game::EntityBatch> batches;
    batches.reserve(tasks.size() / sampling_group + (tasks.size() % sampling_group != 0));
    for (std::size_t first = 0; first < tasks.size(); first += sampling_group) {
        const auto count = std::min(sampling_group, tasks.size() - first);
        batches.emplace_back(std::span(tasks).subspan(first, count), std::span(values).subspan(first, count));
    }
    pool_.run_ranges(batches.size(), 1, [&](memory::IReadChannel& channel, auto first, auto last) {
        for (auto i = first; i < last; ++i)
            batches[i].prepare(channel, directory);
    });
    const auto rules_started_qpc = clock_.now();
    game::AccuracyRules accuracy_rules(reader_, context_);
    pool_.run_ranges(tasks.size(), 1, [&](memory::IReadChannel& channel, auto first, auto last) {
        for (auto i = first; i < last; ++i)
            batches[i / sampling_group].evaluate(channel, context_, directory, i % sampling_group, clock_,
                                                 &accuracy_rules);
    });
    pool_.run_ranges(batches.size(), 1, [&](memory::IReadChannel& channel, auto first, auto last) {
        for (auto i = first; i < last; ++i)
            batches[i].close(channel, bones_);
    });
    const auto changed_rules = accuracy_rules.close();
    const auto rules_finished_qpc = clock_.now();
    for (auto& value : values)
        if (auto* weapon = std::get_if<model::Weapon>(&value); weapon && weapon->accuracy_evaluation) {
            auto& accuracy = *weapon->accuracy_evaluation;
            if (accuracy.used_rules & changed_rules)
                game::revoke_accuracy(accuracy, model::AccuracyStatus::SourceChanged);
            // 开始覆盖共享规则观察；结束覆盖实际使用的共享关闭核验。
            accuracy.started_qpc = rules_started_qpc;
            accuracy.read_finished_qpc = rules_finished_qpc;
        }
    result.local = game::sample_local(reader_, context_, directory);
    result.world = game::sample_world(reader_, context_);
    if (!directory.verify_root(reader_, context_))
        throw memory::ReadUnavailable("Entity directory invalidated before capture submission");
    result.directory_verified = true;
    result.world.map_identity = map_.finish(reader_, context_, map_opening);
    result.interval.read_finished_qpc = clock_.now();
    result.coverage = directory.coverage();
    result.reads = reader_.take_metrics();
    result.reads += pool_.take_metrics();
    for (auto& value : values)
        std::visit(
            [&](auto& entity) {
                using T = std::decay_t<decltype(entity)>;
                if constexpr (std::is_same_v<T, model::Player>)
                    result.players.push_back(std::move(entity));
                else if constexpr (std::is_same_v<T, model::Weapon>)
                    result.weapons.push_back(std::move(entity));
                else if constexpr (std::is_same_v<T, model::Bomb>)
                    result.bombs.push_back(std::move(entity));
                else if constexpr (std::is_same_v<T, model::Projectile>)
                    result.projectiles.push_back(std::move(entity));
                else if constexpr (std::is_same_v<T, model::EntityState>)
                    result.world_objects.push_back(std::move(entity));
            },
            value);
    // 同轮采样的武器结果只读取一次，再按句柄关联到人物；不使用跨轮动态缓存。
    for (auto& player : result.players)
        if (player.body && player.body->active_weapon)
            for (const auto& weapon : result.weapons)
                if (weapon.entity.id == *player.body->active_weapon) {
                    player.body->weapon = weapon;
                    break;
                }
    result.interval.finished_qpc = clock_.now();
    return result;
}
} // namespace cs2::capture
