#include "capture/collector.hpp"
#include "game/cs2/sampling/common.hpp"
#include "game/cs2/sampling/local.hpp"
#include "game/cs2/sampling/player.hpp"
#include "game/cs2/sampling/weapon.hpp"
#include "game/cs2/sampling/world.hpp"
#include "game/cs2/accuracy/rules.hpp"
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
    pool_.run(identities.size(), [&](memory::IReadChannel& channel, std::size_t index) {
        directory.classify(channel, identities[index]);
    });
    directory.resolve_player_aliases(reader_);
    using Value =
        std::variant<std::monostate, model::Player, model::Weapon, model::Bomb, model::Projectile, model::EntityState>;
    std::vector<std::size_t> tasks;
    for (std::size_t i = 0; i < directory.slots.size(); ++i) {
        const auto& slot = directory.slots[i];
        if (slot.state == game::SlotState::Classified && slot.entity.kind != game::EntityKind::PlayerPawn &&
            slot.entity.kind != game::EntityKind::ObserverPawn)
            tasks.push_back(i);
    }
    // 全槽位仍有覆盖终态；仅为实际属性任务分配大结果对象，不为空槽位构造 Player/Weapon 变体。
    std::vector<Value> values(tasks.size());
    const auto rules_started_qpc=clock_.now();
    game::AccuracyRules accuracy_rules(reader_,context_);
    pool_.run(tasks.size(), [&](memory::IReadChannel& channel, std::size_t index) {
        const auto& slot = directory.slots[tasks[index]];
        const auto& ref = slot.entity;
        switch (ref.kind) {
        case game::EntityKind::Controller:
            if (auto value = game::sample_player(channel, context_, directory, ref, bones_))
                values[index] = std::move(*value);
            break;
        case game::EntityKind::Weapon:
            if (auto value = game::sample_weapon(channel, context_, directory, ref, clock_, &accuracy_rules))
                values[index] = std::move(*value);
            break;
        case game::EntityKind::Bomb:
            if (auto value = game::sample_bomb(channel, directory, ref))
                values[index] = std::move(*value);
            break;
        case game::EntityKind::Projectile:
            if (auto value = game::sample_projectile(channel, directory, ref))
                values[index] = std::move(*value);
            break;
        case game::EntityKind::Inferno:
        case game::EntityKind::Hostage:
        case game::EntityKind::Chicken:
            if (directory.verify(channel, ref)) {
                auto value = game::sample_common(channel, ref);
                if (directory.verify(channel, ref))
                    values[index] = std::move(value);
            }
            break;
        default:
            break;
        }
    });
    const auto changed_rules=accuracy_rules.close();
    const auto rules_finished_qpc=clock_.now();
    for (auto& value:values)
        if (auto* weapon=std::get_if<model::Weapon>(&value);weapon && weapon->accuracy_evaluation) {
            auto& accuracy=*weapon->accuracy_evaluation;
            if (accuracy.used_rules&changed_rules)
                game::revoke_accuracy(accuracy,model::AccuracyStatus::SourceChanged);
            // 开始覆盖共享规则观察；结束覆盖实际使用的共享关闭核验。
            accuracy.started_qpc=rules_started_qpc;
            accuracy.read_finished_qpc=rules_finished_qpc;
        }
    result.local = game::sample_local(reader_, context_, directory);
    result.world = game::sample_world(reader_, context_);
    if (!directory.verify_root(reader_, context_))
        throw std::runtime_error("Entity directory invalidated before capture submission");
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
