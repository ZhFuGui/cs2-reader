#include "model/living.hpp"
#include "model/world.hpp"
#include "synchronization/v2/projection_detail.hpp"

namespace cs2::synchronization::v2::projection {
namespace {
void write_aim_punch(Writer& w, unsigned f, const model::AimPunchState& state) {
    Writer child;
    if (state.predictable) {
        Writer component;
        scalar(component, 1, state.predictable->base_time);
        scalar(component, 2, state.predictable->base_angle);
        scalar(component, 3, state.predictable->base_angular_velocity);
        child.message(1, component);
    }
    if (state.unpredictable) {
        Writer component;
        component.sint32(1, state.unpredictable->base_tick);
        scalar(component, 2, state.unpredictable->base_angle);
        child.message(2, component);
    }
    if (state.times) {
        Writer times;
        times.fixed32(1, state.times->world_group);
        times.uint32(2, static_cast<std::uint32_t>(state.times->visual_policy));
        if (state.times->simulation)
            scalar(times, 3, *state.times->simulation);
        if (state.times->visual_predictable)
            scalar(times, 4, *state.times->visual_predictable);
        if (state.times->visual_unpredictable)
            scalar(times, 5, *state.times->visual_unpredictable);
        times.real(6, 1.0f / 64.0f);
        put(times, 7, state.times->native_context);
        child.message(3, times);
    }
    w.message(f, child);
}
} // namespace

Writer entity(const model::EntityState& v) {
    Writer w;
    scalar(w, 1, v.id);
    w.text(2, v.designer_name);
    put(w, 3, v.transform.position);
    put(w, 4, v.transform.rotation);
    put(w, 5, v.transform.scale);
    put(w, 6, v.transform.dormant);
    put(w, 7, v.world_velocity);
    put(w, 8, v.team);
    put(w, 9, v.health);
    put(w, 10, v.max_health);
    put(w, 11, v.life_state);
    put(w, 12, v.flags);
    put(w, 13, v.owner);
    return w;
}
Writer weapon(const model::Weapon& v) {
    Writer w;
    w.message(1, entity(v.entity));
    put(w, 2, v.definition_index);
    put(w, 3, v.custom_name);
    put(w, 4, v.clip);
    put(w, 5, v.secondary_clip);
    put(w, 6, v.reserve_ammo);
    put(w, 7, v.paint_kit);
    put(w, 8, v.wear);
    put(w, 9, v.stattrak);
    put(w, 10, v.accuracy_penalty);
    put(w, 11, v.recoil_index);
    put(w, 12, v.reloading);
    put(w, 13, v.silenced);
    put(w, 14, v.burst_mode);
    if (v.accuracy_evaluation) {
        const auto& a = *v.accuracy_evaluation;
        Writer group;
        group.text(1, a.model_id);
        put(group, 2, a.owner);
        const auto accuracy_scalar = [&](unsigned tag, const model::AccuracyScalar& value) {
            if ((value.status == model::AccuracyStatus::Valid) != value.value.has_value() ||
                (value.value && !std::isfinite(*value.value)))
                throw wire::Error("invalid accuracy scalar presence");
            Writer scalar_value;
            put(scalar_value, 1, value.value);
            scalar_value.uint32(2, static_cast<unsigned>(value.status));
            group.message(tag, scalar_value);
        };
        accuracy_scalar(3, a.inaccuracy);
        accuracy_scalar(4, a.spread);
        group.uint32(5, static_cast<unsigned>(a.branch));
        put(group, 6, a.movement);
        put(group, 7, a.airborne);
        put(group, 8, a.turning);
        put(group, 9, a.lateral);
        put(group, 10, a.penalty);
        put(group, 11, a.mode_raw);
        put(group, 12, a.accuracy_state_time_seconds);
        (void)group.fixed64(13, a.started_qpc);
        (void)group.fixed64(14, a.read_finished_qpc);
        w.message(15, group);
    }
    return w;
}
namespace {
Writer body(const model::PlayerBody& v) {
    Writer w;
    w.message(1, entity(v.entity));
    put(w, 2, v.armor);
    put(w, 3, v.shots_fired);
    put(w, 4, v.scoped);
    put(w, 5, v.defusing);
    put(w, 6, v.helmet);
    put(w, 7, v.defuser);
    put(w, 8, v.spotted);
    if (v.spotted_by) {
        Writer mask;
        mask.fixed32(1, (*v.spotted_by)[0]);
        mask.fixed32(2, (*v.spotted_by)[1]);
        w.message(9, mask);
    }
    put(w, 10, v.flash_duration);
    put(w, 11, v.flash_max_alpha);
    put(w, 12, v.eye_angles);
    if (v.aim_punch)
        write_aim_punch(w, 13, *v.aim_punch);
    put(w, 20, v.input_eye_angles);
    if (v.eye_basis) {
        Writer basis;
        scalar(basis, 1, v.eye_basis->value);
        basis.uint32(2, static_cast<std::uint32_t>(v.eye_basis->source));
        basis.sint32(3, v.eye_basis->native_context);
        put(basis, 4, v.eye_basis->interpolation_parameter);
        if (v.eye_basis->resolved_source)
            basis.uint32(5, static_cast<std::uint32_t>(*v.eye_basis->resolved_source));
        put(basis, 6, v.eye_basis->source_entity);
        if (v.eye_basis->delegation_depth)
            basis.uint32(7, v.eye_basis->delegation_depth);
        w.message(21, basis);
    }
    put(w, 15, v.view_offset);
    put(w, 16, v.location);
    put(w, 17, v.active_weapon);
    if (v.skeleton) {
        Writer skeleton;
        for (const auto& bone : v.skeleton->points) {
            Writer point;
            point.uint32(1, bone.index);
            scalar(point, 2, bone.position);
            if (bone.role)
                point.uint32(3, static_cast<std::uint32_t>(*bone.role));
            if (bone.world_pose) {
                if (!model::unit_quaternion(bone.world_pose->rotation) ||
                    !std::isfinite(bone.world_pose->uniform_scale))
                    throw wire::Error("invalid bone world pose");
                scalar(point, 4, bone.world_pose->rotation);
                point.real(5, bone.world_pose->uniform_scale);
            }
            skeleton.message(1, point);
        }
        skeleton.uint32(2, static_cast<std::uint32_t>(v.skeleton->status));
        put(skeleton, 3, v.skeleton->model_name);
        put(skeleton, 4, v.skeleton->profile_id);
        put(skeleton, 6, v.skeleton->native_hitbox_set_available);
        for (const auto& edge : v.skeleton->links) {
            Writer link;
            link.uint32(1, edge.parent_index);
            link.uint32(2, edge.child_index);
            skeleton.message(5, link);
        }
        w.message(18, skeleton);
    }
    // 现有模型没有已核验空列表的存在性，不将空vector伪造成成功列表。
    // equipped_weapons整体暂不投影；非空vector同样不能证明名单读取完整成功。
    return w;
}
} // namespace

Writer player(const model::Player& v) {
    Writer w;
    scalar(w, 1, v.controller);
    put(w, 2, v.name);
    put(w, 3, v.sanitized_name);
    if (v.steam_id)
        (void)w.fixed64(4, *v.steam_id);
    put(w, 5, v.connection_state);
    put(w, 6, v.ping);
    put(w, 7, v.score);
    put(w, 8, v.money);
    put(w, 9, v.bot_difficulty);
    put(w, 10, v.team);
    put(w, 11, v.local);
    put(w, 12, v.controller_reports_alive);
    put(w, 13, v.controller_reported_health);
    put(w, 14, v.controller_reported_armor);
    put(w, 15, v.player_pawn);
    put(w, 16, v.observer_pawn);
    if (v.body)
        w.message(17, body(*v.body));
    return w;
}
Writer bomb(const model::Bomb& v) {
    Writer w;
    w.message(1, entity(v.entity));
    put(w, 2, v.ticking);
    put(w, 3, v.defused);
    put(w, 4, v.being_defused);
    put(w, 5, v.exploded);
    put(w, 6, v.site);
    put(w, 7, v.explode_at);
    put(w, 8, v.timer_length);
    put(w, 9, v.defuse_at);
    put(w, 10, v.defuse_length);
    put(w, 11, v.defuser);
    return w;
}
Writer projectile(const model::Projectile& v) {
    Writer w;
    w.message(1, entity(v.entity));
    put(w, 2, v.initial_position);
    put(w, 3, v.initial_velocity);
    put(w, 4, v.bounces);
    put(w, 5, v.explode_tick);
    put(w, 6, v.smoke_tick);
    put(w, 7, v.exploded);
    put(w, 8, v.smoke_started);
    put(w, 9, v.spawn_time);
    return w;
}
} // namespace cs2::synchronization::v2::projection
