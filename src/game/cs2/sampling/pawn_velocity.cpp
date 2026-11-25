#include "game/cs2/sampling/pawn_velocity.hpp"
#include "game/cs2/accuracy/dispatch.hpp"
#include "game/cs2/entity/directory.hpp"
#include "game/cs2/entity/layout.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "memory/read_plan.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace cs2::game {
namespace {
namespace p = accuracy_profile;
namespace s = offsets::schema;
using accuracy_dispatch::Failure;
using accuracy_dispatch::Status;
using accuracy_dispatch::supports_accuracy;
using accuracy_dispatch::verify_entry;
using memory::Address;

model::Vec3 reconstruct_local_velocity(memory::Observation& proof, memory::IReadChannel& reader, const Context& context,
                                       Address owner) {
    verify_entry(context, reader, proof, p::velocity_parent);
    verify_entry(context, reader, proof, p::velocity_source);
    const auto storage = owner + s::C_BaseEntity::m_vecVelocity;
    const auto clock_address = context.client.base + p::velocity_time;
    constexpr std::size_t active_offset = 0xc0, flags_offset = 0x98, dirty_offset = 0x99;
    constexpr std::size_t cached_value_offset = 0x30, cached_time_offset = 0x80, lane_stride = 0x28;
    struct VelocityClock {
        std::int32_t context, mode;
        std::array<float, 2> parameter;
    } timing{};
    static_assert(sizeof(timing) == 16);
    std::array<std::byte, active_offset + 1> bytes{};
    std::uint32_t lock_before{}, lock_after{};
    Address parent{};
    memory::ReadPlan plan(6);
    const auto before = plan.add(owner + p::velocity_mutex, lock_before);
    const auto scene = plan.pointer(owner + s::C_BaseEntity::m_pGameSceneNode);
    const auto parent_field = plan.add_from(scene, s::CGameSceneNode::m_pParent, parent);
    const auto source = plan.add(storage, bytes.data(), bytes.size());
    const auto clock = plan.add(clock_address, timing);
    const auto after = plan.add(owner + p::velocity_mutex, lock_after);
    plan.execute(reader);
    const auto node = plan.pointer_value(scene);
    if (!plan.complete(before) || !plan.complete(after) || !node || !memory::user_range(*node) ||
        !plan.complete(parent_field) || !plan.complete(source))
        throw Failure{Status::ReadFailed};
    if (lock_before || lock_after || parent)
        throw Failure{Status::DirtyVelocity};

    const auto field = [&]<class T>(std::size_t offset) {
        T value{};
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
        return value;
    };
    proof.remember(owner + s::C_BaseEntity::m_pGameSceneNode, *node);
    proof.remember(*node + s::CGameSceneNode::m_pParent, parent);
    proof.remember(owner + p::velocity_mutex, lock_before);
    const auto active = field.operator()<std::uint8_t>(active_offset);
    proof.remember(storage + active_offset, active);
    int lane = -1;
    const auto flags = field.operator()<std::uint8_t>(flags_offset);
    if (active) {
        if (!plan.complete(clock))
            throw Failure{Status::ReadFailed};
        if (timing.context == -1 || (timing.context == 0 && timing.mode == 1))
            lane = 0;
        else if ((timing.context == 0 && (flags & 0x20)) || (timing.context == 1 && timing.mode == 1))
            lane = (flags & 0x20) ? 1 : 0;
        proof.remember(clock_address, timing.context);
        if (timing.context == 0 || timing.context == 1)
            proof.remember(clock_address + sizeof(timing.context), timing.mode);
        if (lane >= 0 || (timing.context == 0 && timing.mode != 1))
            proof.remember(storage + flags_offset, flags);
    }
    std::size_t value_offset = 0;
    if (lane >= 0) {
        verify_entry(context, reader, proof, p::velocity_cache);
        const auto time_index = flags >> 7;
        const auto time = timing.parameter[time_index];
        const auto time_offset = cached_time_offset + lane * sizeof(float);
        const auto cached_at = field.operator()<float>(time_offset);
        if (!std::isfinite(time) || !std::isfinite(cached_at))
            throw Failure{Status::InvalidInput};
        // 时间不等时，只接受原生会直接保留该槽的分支；其余分支需要完整插值历史。
        if (time != cached_at) {
            const auto dirty = field.operator()<std::uint8_t>(dirty_offset);
            if (time < cached_at || (dirty & (0x40u << lane)))
                throw Failure{Status::DirtyVelocity};
            proof.remember(storage + dirty_offset, dirty);
        }
        proof.remember(clock_address + offsetof(VelocityClock, parameter) + time_index * sizeof(float), time);
        proof.remember(storage + time_offset, cached_at);
        value_offset = cached_value_offset + lane * lane_stride;
    }
    const auto velocity = field.operator()<model::Vec3>(value_offset);
    if (!model::finite(velocity))
        throw Failure{Status::InvalidInput};
    proof.remember(storage + value_offset, velocity);
    return velocity;
}
} // namespace

void sample_pawn_velocities(memory::IReadChannel& reader, const Context& context, const Directory& directory,
                            std::span<PawnVelocity> samples, const LocalAccuracyTarget* local) {
    if (samples.empty())
        return;
    using namespace s::C_BaseEntity;
    const auto reset = [](PawnVelocity& sample) {
        sample.value.reset();
        sample.owner.reset();
        sample.reconstructed_for.reset();
        sample.status = Status::ReadFailed;
    };
    if (!supports_accuracy(context)) {
        // 仍可提供 schema 中的缓存字段。原生入口及锁的布局按镜像核验。
        std::vector<model::Vec3> cached(samples.size());
        std::vector<std::uint32_t> owners(samples.size());
        std::vector<memory::ReadRequest> reads;
        reads.reserve(samples.size() * 2);
        for (std::size_t i = 0; i < samples.size(); ++i) {
            reads.push_back({samples[i].pawn.address + m_vecAbsVelocity, &cached[i], sizeof(model::Vec3)});
            reads.push_back({samples[i].pawn.address + m_hOwnerEntity, &owners[i], sizeof(std::uint32_t)});
        }
        reader.read_many(reads);
        for (std::size_t i = 0; i < samples.size(); ++i) {
            reset(samples[i]);
            samples[i].status = Status::UnsupportedBuild;
            if (reads[2 * i].result.complete(sizeof(model::Vec3)) && model::finite(cached[i]))
                samples[i].value = cached[i];
            if (reads[2 * i + 1].result.complete(sizeof(std::uint32_t)) && owners[i] != UINT32_MAX)
                samples[i].owner = model::EntityId{owners[i]};
        }
        return;
    }
    struct State {
        std::uint32_t before{}, after{}, flags{};
        std::uint32_t owner = UINT32_MAX;
        std::uint8_t move_type{};
        model::Vec3 cached{};
        memory::ReadPlan::Ticket before_read{}, after_read{}, flags_read{}, type_read{}, cache_read{};
        memory::ReadPlan::Ticket owner_read{};
        void schedule(memory::ReadPlan& plan, Address pawn, bool read_cache = true) {
            // 引擎会先清脏标志再写速度，两次锁观察围住标志和值。
            before_read = plan.add(pawn + p::velocity_mutex, before);
            flags_read = plan.add(pawn + m_iEFlags, flags);
            type_read = plan.add(pawn + m_nActualMoveType, move_type);
            if (read_cache)
                cache_read = plan.add(pawn + m_vecAbsVelocity, cached);
            after_read = plan.add(pawn + p::velocity_mutex, after);
        }
        bool state_read(const memory::ReadPlan& plan) const {
            return plan.complete(before_read) && plan.complete(after_read) && plan.complete(flags_read) &&
                   plan.complete(type_read);
        }
    };
    std::vector<State> opened(samples.size()), closed(samples.size());
    memory::ReadPlan opening(samples.size() * 6);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const auto pawn = samples[i].pawn.address;
        opened[i].schedule(opening, pawn);
        opened[i].owner_read = opening.add(pawn + m_hOwnerEntity, opened[i].owner);
    }
    opening.execute(reader);

    memory::ReadPlan closing(samples.size() * 5);
    bool verification_needed = false;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        auto& sample = samples[i];
        reset(sample);
        const auto& seen = opened[i];
        if (opening.complete(seen.owner_read) && seen.owner != UINT32_MAX)
            sample.owner = model::EntityId{seen.owner};
        if (!seen.state_read(opening))
            continue;
        if (seen.before || seen.after) {
            sample.status = Status::DirtyVelocity;
            continue;
        }
        const bool dirty = (seen.flags & 0x1000) != 0;
        if (seen.move_type == 5 || (dirty && (!local || &sample.pawn != &local->pawn))) {
            // 保留本帧通用实体的引擎缓存。其状态不允许作为 Inaccuracy 的输入。
            if (opening.complete(seen.cache_read) && model::finite(seen.cached))
                sample.value = seen.cached;
            sample.status = seen.move_type == 5 ? Status::UnsupportedPhysics : Status::DirtyVelocity;
            continue;
        }
        if (!dirty) {
            if (!opening.complete(seen.cache_read))
                continue;
            if (!model::finite(seen.cached)) {
                sample.status = Status::InvalidInput;
                continue;
            }
            sample.value = seen.cached;
        } else {
            try {
                memory::Observation proof(reader);
                const auto owner = sample.pawn.address;
                proof.remember(owner + p::velocity_mutex, seen.before);
                proof.remember(context.client.base + offsets::client::dwLocalPlayerController,
                               local->controller.address);
                proof.remember(local->controller.address + s::CBasePlayerController::m_hPawn, sample.pawn.handle);
                proof.remember(owner + s::C_BasePlayerPawn::m_pWeaponServices, local->weapon_services);
                proof.remember(local->weapon_services + s::CPlayer_WeaponServices::m_hActiveWeapon,
                               local->weapon.handle);
                const auto value = reconstruct_local_velocity(proof, reader, context, owner);
                if (!proof.close() || !directory.verify(reader, local->controller))
                    throw Failure{Status::SourceChanged};
                sample.value = value;
                sample.reconstructed_for = model::EntityId{local->weapon.handle};
            } catch (const Failure& failure) {
                sample.status = failure.status;
                continue;
            }
        }
        closed[i].schedule(closing, sample.pawn.address, !sample.reconstructed_for);
        sample.status = Status::Valid;
        verification_needed = true;
    }
    if (!verification_needed)
        return;

    Status dispatch = Status::Valid;
    memory::Observation code(reader);
    try {
        verify_entry(context, reader, code, p::velocity);
        verify_entry(context, reader, code, p::velocity_recompute);
        if (!code.close())
            dispatch = Status::SourceChanged;
    } catch (const Failure& failure) {
        dispatch = failure.status;
    }
    closing.execute(reader);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        auto& sample = samples[i];
        if (sample.status != Status::Valid)
            continue;
        const auto& first = opened[i];
        const auto& last = closed[i];
        const bool cache_matches =
            sample.reconstructed_for ||
            (closing.complete(last.cache_read) && std::memcmp(&first.cached, &last.cached, sizeof(model::Vec3)) == 0);
        if (!last.state_read(closing) || last.before || last.after || last.flags != first.flags ||
            last.move_type != first.move_type || !cache_matches || dispatch != Status::Valid) {
            sample.value.reset();
            sample.status = dispatch == Status::Valid ? Status::SourceChanged : dispatch;
        }
    }
}

const PawnVelocity* find_pawn_velocity(std::span<const PawnVelocity> samples, std::uint32_t handle) {
    const auto index = handle & layout::entity_index_mask;
    const auto found =
        std::ranges::lower_bound(samples, index, {}, [](const auto& sample) { return sample.pawn.index; });
    return found != samples.end() && found->pawn.handle == handle ? &*found : nullptr;
}

std::optional<LocalAccuracyTarget> sample_local_accuracy_target(memory::IReadChannel& reader, const Context& context,
                                                                const Directory& directory) {
    if (!supports_accuracy(context))
        return {};
    std::uint32_t pawn_handle{};
    memory::ReadPlan local(2);
    const auto controller_ticket = local.pointer(context.client.base + offsets::client::dwLocalPlayerController);
    const auto pawn_ticket = local.add_from(controller_ticket, s::CBasePlayerController::m_hPawn, pawn_handle);
    local.execute(reader);
    const auto controller_address = local.pointer_value(controller_ticket);
    if (!controller_address || !memory::user_range(*controller_address) || !local.complete(pawn_ticket))
        return {};
    const auto* controller = directory.find_address(*controller_address);
    const auto* pawn = directory.resolve(pawn_handle);
    if (!controller || controller->kind != EntityKind::Controller || !pawn || pawn->kind != EntityKind::PlayerPawn)
        return {};

    std::uint32_t active_handle{};
    memory::ReadPlan active(2);
    const auto services = active.pointer(pawn->address + s::C_BasePlayerPawn::m_pWeaponServices);
    const auto weapon_ticket = active.add_from(services, s::CPlayer_WeaponServices::m_hActiveWeapon, active_handle);
    active.execute(reader);
    const auto weapon_services = active.pointer_value(services);
    const auto* weapon = active.complete(weapon_ticket) ? directory.resolve(active_handle) : nullptr;
    if (!weapon_services || !memory::user_range(*weapon_services) || !weapon || weapon->kind != EntityKind::Weapon)
        return {};
    return LocalAccuracyTarget{*controller, *pawn, *weapon, *weapon_services};
}
} // namespace cs2::game
