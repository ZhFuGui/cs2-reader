#include "game/cs2/capture/entity_sampler.hpp"
#include "game/cs2/accuracy/rules.hpp"
#include "game/cs2/sampling/local.hpp"
#include "game/cs2/sampling/pawn_velocity.hpp"
#include "model/snapshot.hpp"
#include <algorithm>
#include <type_traits>
#include <utility>
#include <variant>

namespace cs2::game {
namespace {
constexpr std::size_t classification_group = 16, sampling_group = 8;
} // namespace

EntitySampler::EntitySampler(const memory::ISession& session, const Context& context, const capture::IClock& clock,
                             std::size_t workers, bool bones)
    : worker_contexts_(workers, context), pool_(session, workers), clock_(clock), bones_(bones) {}

Directory EntitySampler::discover(memory::IReadChannel& reader, const Context& context) {
    Directory directory;
    directory.discover(reader, context);
    std::vector<std::size_t> identities;
    for (std::size_t i = 0; i < directory.slots.size(); ++i)
        if (directory.slots[i].state == SlotState::Pending)
            identities.push_back(i);
    // 只决定每个 worker 一次推进多少对象；最终循环仍覆盖全部任务。
    pool_.run_ranges_indexed(identities.size(), classification_group,
                             [&](memory::IReadChannel& channel, auto worker, auto first, auto last) {
                                 directory.classify_many(channel, std::span(identities).subspan(first, last - first),
                                                         &worker_contexts_[worker]);
                             });
    directory.resolve_player_aliases(reader);
    return directory;
}

EntitySamples EntitySampler::sample(memory::IReadChannel& reader, const Context& context, const Directory& directory) {
    std::vector<const EntityRef*> tasks;
    std::vector<PawnVelocity> velocities;
    for (const auto& slot : directory.slots) {
        if (slot.state != SlotState::Classified)
            continue;
        if (slot.entity.kind == EntityKind::PlayerPawn)
            velocities.emplace_back(slot.entity);
        else if (slot.entity.kind != EntityKind::ObserverPawn)
            tasks.push_back(&slot.entity);
    }
    // 全槽位仍有覆盖终态；仅为实际属性任务分配大结果对象，不为空槽位构造 Player/Weapon 变体。
    EntitySamples result;
    auto& values = result.values;
    values.resize(tasks.size());
    std::vector<EntityBatch> batches;
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
    AccuracyRules accuracy_rules(reader, context);
    const auto local_accuracy = sample_local_accuracy_target(reader, context, directory);
    const auto* local = local_accuracy ? &*local_accuracy : nullptr;
    pool_.run_ranges_indexed(velocities.size(), sampling_group,
                             [&](memory::IReadChannel& channel, auto worker, auto first, auto last) {
                                 sample_pawn_velocities(channel, worker_contexts_[worker], directory,
                                                        std::span(velocities).subspan(first, last - first), local);
                             });
    pool_.run_ranges_indexed(tasks.size(), 1, [&](memory::IReadChannel& channel, auto worker, auto first, auto last) {
        for (auto i = first; i < last; ++i)
            batches[i / sampling_group].evaluate(channel, worker_contexts_[worker], directory, i % sampling_group,
                                                 clock_, &accuracy_rules, velocities);
    });
    pool_.run_ranges(batches.size(), 1, [&](memory::IReadChannel& channel, auto first, auto last) {
        for (auto i = first; i < last; ++i)
            batches[i].close(channel, bones_);
    });
    const auto changed_rules = accuracy_rules.close();
    const auto rules_finished_qpc = clock_.now();
    result.local = sample_local(reader, context, directory);
    const auto local_finished_qpc = clock_.now();
    const bool local_accuracy_matches =
        local_accuracy && result.local.controller == model::EntityId{local_accuracy->controller.handle} &&
        result.local.pawn == model::EntityId{local_accuracy->pawn.handle} &&
        std::ranges::any_of(values, [&](const EntityValue& value) {
            const auto* player = std::get_if<model::Player>(&value);
            return player && player->controller.handle == local_accuracy->controller.handle && player->body &&
                   player->body->entity.id.handle == local_accuracy->pawn.handle &&
                   player->body->active_weapon == model::EntityId{local_accuracy->weapon.handle};
        });
    for (auto& value : values) {
        if (auto* player = std::get_if<model::Player>(&value); player && player->body) {
            const auto speed = find_pawn_velocity(velocities, player->body->entity.id.handle);
            if (speed) {
                player->body->entity.owner = speed->owner;
                if (!speed->reconstructed_for || local_accuracy_matches)
                    player->body->entity.world_velocity = speed->value;
            }
        } else if (auto* weapon = std::get_if<model::Weapon>(&value); weapon && weapon->accuracy_evaluation) {
            auto& accuracy = *weapon->accuracy_evaluation;
            const bool local_weapon = local_accuracy && weapon->entity.id.handle == local_accuracy->weapon.handle;
            if (local_weapon && !local_accuracy_matches) {
                revoke_accuracy(accuracy, model::AccuracyStatus::SourceChanged);
                accuracy.owner.reset();
            } else if (accuracy.used_rules & changed_rules) {
                revoke_accuracy(accuracy, model::AccuracyStatus::SourceChanged);
            }
            // 局部精度组的结束时刻包含本地归属复核，其余精度组结束于共享规则核验。
            accuracy.started_qpc = rules_started_qpc;
            accuracy.read_finished_qpc = local_weapon ? local_finished_qpc : rules_finished_qpc;
        }
    }
    return result;
}

void EntitySamples::append_to(model::Snapshot& result) && {
    result.local = std::move(local);
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
}
} // namespace cs2::game
