#include "game/cs2/sampling/batch.hpp"
#include "game/cs2/entity/identity_observation.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "game/cs2/sampling/accuracy.hpp"
#include "game/cs2/sampling/common.hpp"
#include "game/cs2/sampling/direction.hpp"
#include "game/cs2/sampling/player.hpp"
#include "game/cs2/sampling/skeleton.hpp"
#include "game/cs2/sampling/weapon.hpp"
#include "game/cs2/sampling/world.hpp"
#include <stdexcept>

namespace cs2::game {
namespace {
memory::Fields root_fields(const EntityRef& ref) {
    switch (ref.kind) {
    case EntityKind::Controller:
        return player_fields(ref.address);
    case EntityKind::Weapon:
        return weapon_fields(ref.address);
    case EntityKind::Bomb:
        return bomb_fields(ref.address);
    case EntityKind::Projectile:
        return projectile_fields(ref);
    default:
        return memory::Fields(ref.address, entity_fields());
    }
}
} // namespace
EntityBatch::EntityBatch(std::span<const EntityRef* const> refs, std::span<EntityValue> values)
    : refs_(refs), values_(values), roots_(refs.size()), bodies_(refs.size()) {
    if (refs.size() != values.size())
        throw std::invalid_argument("Entity batch input and output sizes differ");
    for (auto& value : values)
        value = std::monostate{};
}
void EntityBatch::prepare(memory::IReadChannel& reader, const Directory& directory) {
    if (phase_ != Phase::Created)
        throw std::logic_error("Entity batch can only be prepared once");
    phase_ = Phase::Preparing;
    const auto refs = refs_;
    const auto values = values_;
    auto& roots = roots_;
    auto& bodies = bodies_;
    const auto opening = directory.verify_many(reader, refs);
    std::vector<memory::Fields*> fields;
    for (std::size_t i = 0; i < refs.size(); ++i)
        if (opening[i]) {
            roots[i].emplace(Prepared{*refs[i], root_fields(*refs[i]), {}});
            fields.push_back(&roots[i]->fields);
        }
    memory::Fields::read_many(reader, fields);

    // Controller 字段完成后才能建立 Pawn 的本轮依赖；不从旧帧借用身体地址。
    std::vector<const EntityRef*> body_refs;
    std::vector<std::size_t> body_owners;
    for (std::size_t i = 0; i < roots.size(); ++i) {
        if (!roots[i] || roots[i]->ref.kind != EntityKind::Controller)
            continue;
        const auto handle = roots[i]->fields.get<std::uint32_t>(offsets::schema::CCSPlayerController::m_hPlayerPawn);
        const auto* body = handle ? directory.resolve(*handle) : nullptr;
        if (body && body->kind == EntityKind::PlayerPawn) {
            body_refs.push_back(body);
            body_owners.push_back(i);
        }
    }
    const auto body_opening = directory.verify_many(reader, body_refs);
    fields.clear();
    for (std::size_t i = 0; i < body_refs.size(); ++i)
        if (body_opening[i]) {
            auto& body = bodies[body_owners[i]];
            body.emplace(Prepared{*body_refs[i], body_fields(body_refs[i]->address), {}});
            fields.push_back(&body->fields);
        }
    memory::Fields::read_many(reader, fields);

    std::vector<TransformRead> transforms;
    for (std::size_t i = 0; i < roots.size(); ++i) {
        if (roots[i] && roots[i]->ref.kind != EntityKind::Controller)
            transforms.push_back({roots[i]->ref, roots[i]->fields, roots[i]->transform});
        if (bodies[i])
            transforms.push_back({bodies[i]->ref, bodies[i]->fields, bodies[i]->transform});
    }
    sample_transforms(reader, transforms);

    for (std::size_t i = 0; i < roots.size(); ++i) {
        if (!roots[i])
            continue;
        const auto sample = roots[i]->sample();
        switch (sample.ref.kind) {
        case EntityKind::Controller: {
            const auto body = bodies[i] ? std::optional(bodies[i]->sample()) : std::nullopt;
            values[i] = project_player(sample, body ? &*body : nullptr);
            break;
        }
        case EntityKind::Weapon:
            values[i] = project_weapon(sample);
            break;
        case EntityKind::Bomb:
            values[i] = sample_bomb(sample);
            break;
        case EntityKind::Projectile:
            values[i] = sample_projectile(sample);
            break;
        case EntityKind::Inferno:
        case EntityKind::Hostage:
        case EntityKind::Chicken:
            values[i] = project_common(sample);
            break;
        default:
            break;
        }
    }
    phase_ = Phase::Prepared;
}
void EntityBatch::evaluate(memory::IReadChannel& reader, const Context& context, const Directory& directory,
                           std::size_t index, const capture::IClock& clock, const AccuracyRules* rules) {
    if (phase_ != Phase::Prepared || index >= values_.size())
        throw std::logic_error("Entity evaluation outside its prepared batch");
    if (auto* player = std::get_if<model::Player>(&values_[index]); player && player->body) {
        auto direction = sample_direction(reader, context, directory, bodies_[index]->ref);
        player->body->eye_basis = std::move(direction.eye_basis);
        player->body->aim_punch = std::move(direction.aim_punch);
    } else if (auto* weapon = std::get_if<model::Weapon>(&values_[index])) {
        weapon->accuracy_evaluation =
            sample_weapon_accuracy(reader, context, directory, roots_[index]->ref, clock, rules);
    }
}
void EntityBatch::close(memory::IReadChannel& reader, bool bones) {
    if (phase_ != Phase::Prepared)
        throw std::logic_error("Entity batch close outside its prepared phase");
    phase_ = Phase::Closed;
    const auto refs = refs_;
    const auto values = values_;
    auto& roots = roots_;
    auto& bodies = bodies_;
    std::vector<PlayerSupplementRead> supplements;
    for (std::size_t i = 0; i < roots.size(); ++i)
        if (roots[i])
            if (auto* player = std::get_if<model::Player>(&values[i]))
                supplements.push_back(
                    {roots[i]->sample(), bodies[i] ? std::optional(bodies[i]->sample()) : std::nullopt, *player});
    sample_player_supplements(reader, supplements);
    if (bones) {
        std::vector<SkeletonRead> skeletons;
        for (std::size_t i = 0; i < bodies.size(); ++i)
            if (bodies[i])
                if (auto* player = std::get_if<model::Player>(&values[i]); player && player->body) {
                    player->body->skeleton.emplace();
                    skeletons.push_back(
                        {bodies[i]->ref.address,
                         bodies[i]->fields.get<memory::Address>(offsets::schema::C_BaseEntity::m_pGameSceneNode),
                         *player->body->skeleton});
                }
        sample_skeletons(reader, skeletons);
    }

    // 静态身份及 Controller→Pawn 来源统一关闭；各深层求值器仍保留自己的紧邻关闭，
    // 不把动态命令/时间等观察拉长到整个对象组。
    struct Closing {
        explicit Closing(const EntityRef& ref) : identity(ref) {}
        IdentityObservation identity;
        std::uint32_t pawn{};
        std::optional<memory::ReadPlan::Ticket> pawn_ticket;
    };
    std::vector<std::optional<Closing>> root_closing(refs.size()), body_closing(refs.size());
    memory::ReadPlan closing;
    for (std::size_t i = 0; i < roots.size(); ++i) {
        if (roots[i]) {
            auto& close = root_closing[i].emplace(roots[i]->ref);
            close.identity.schedule(closing);
            if (auto* player = std::get_if<model::Player>(&values[i]); player && player->player_pawn)
                close.pawn_ticket = closing.add(
                    roots[i]->ref.address + offsets::schema::CCSPlayerController::m_hPlayerPawn, close.pawn);
        }
        if (bodies[i])
            body_closing[i].emplace(bodies[i]->ref).identity.schedule(closing);
    }
    closing.execute(reader);
    for (std::size_t i = 0; i < roots.size(); ++i) {
        if (!roots[i])
            continue;
        const auto& close = *root_closing[i];
        if (!close.identity.matches(closing)) {
            values[i] = std::monostate{};
            continue;
        }
        if (auto* player = std::get_if<model::Player>(&values[i])) {
            if (body_closing[i] && !body_closing[i]->identity.matches(closing))
                player->body.reset();
            if (close.pawn_ticket &&
                (!closing.complete(*close.pawn_ticket) || close.pawn != player->player_pawn->handle)) {
                player->player_pawn.reset();
                player->body.reset();
            }
        }
    }
}
} // namespace cs2::game
