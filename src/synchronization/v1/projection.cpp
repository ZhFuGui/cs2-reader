#include "synchronization/v1/projection.hpp"

namespace cs2::synchronization::v1 {
namespace {
using wire::Writer;
void scalar(Writer& w, unsigned f, bool v) {
    w.boolean(f, v);
}
void scalar(Writer& w, unsigned f, std::int32_t v) {
    w.sint32(f, v);
}
void scalar(Writer& w, unsigned f, std::uint32_t v) {
    w.uint32(f, v);
}
void scalar(Writer& w, unsigned f, std::uint16_t v) {
    w.uint32(f, v);
}
void scalar(Writer& w, unsigned f, std::uint8_t v) {
    w.uint32(f, v);
}
void scalar(Writer& w, unsigned f, float v) {
    w.real(f, v);
}
void scalar(Writer& w, unsigned f, const std::string& v) {
    w.text(f, v);
}
void scalar(Writer& w, unsigned f, model::EntityId v) {
    Writer child;
    child.fixed32(1, v.handle);
    w.message(f, child);
}
void scalar(Writer& w, unsigned f, model::Vec3 v) {
    Writer child;
    child.real(1, v.x);
    child.real(2, v.y);
    child.real(3, v.z);
    w.message(f, child);
}
void scalar(Writer& w, unsigned f, model::Angles v) {
    Writer child;
    child.real(1, v.pitch);
    child.real(2, v.yaw);
    child.real(3, v.roll);
    w.message(f, child);
}
void scalar(Writer& w, unsigned f, model::Quaternion v) {
    Writer child;
    child.real(1, v.x);
    child.real(2, v.y);
    child.real(3, v.z);
    child.real(4, v.w);
    w.message(f, child);
}
template <class T> void put(Writer& w, unsigned f, const std::optional<T>& v) {
    if (v)
        scalar(w, f, *v);
}
void scalar(Writer& w, unsigned f, model::GameTimePoint time) {
    Writer child;
    child.sint32(1, time.tick);
    child.real(2, time.fraction);
    w.message(f, child);
}
void scalar(Writer& w, unsigned f, const model::AimPunchState& state) {
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
void scalar(Writer& w, unsigned f, const model::CameraViewPunchState& state) {
    Writer child;
    scalar(child, 1, state.base_angle);
    scalar(child, 2, state.base_time);
    put(child, 3, state.evaluation_time_seconds);
    put(child, 4, state.decay_exponent);
    w.message(f, child);
}
void scalar(Writer& w, unsigned f, const model::MouseMapping& state) {
    Writer child;
    if (state.source)
        child.uint32(1, static_cast<std::uint32_t>(*state.source));
    put(child, 2, state.sensitivity);
    put(child, 3, state.vertical_scale);
    put(child, 4, state.yaw);
    put(child, 5, state.pitch);
    put(child, 6, state.zoom_ratio);
    put(child, 7, state.inverted_y);
    put(child, 8, state.effective_pitch);
    put(child, 9, state.pawn_fov_adjust);
    put(child, 10, state.pawn_mouse_sensitivity);
    put(child, 11, state.yaw_degrees_per_delta);
    put(child, 12, state.pitch_degrees_per_delta);
    w.message(f, child);
}
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
        scalar(w, 13, *v.aim_punch);
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
                if (!model::unit_quaternion(bone.world_pose->rotation) || !std::isfinite(bone.world_pose->uniform_scale))
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
Writer local(const model::Local& v) {
    Writer w;
    put(w, 1, v.controller);
    put(w, 2, v.pawn);
    put(w, 3, v.observer_target);
    put(w, 4, v.view_entity);
    if (v.view_matrix) {
        Writer matrix;
        // protobuf允许packed字段使用等价的逐元素wire形式。
        for (auto value : *v.view_matrix)
            matrix.real(1, value);
        w.message(5, matrix);
    }
    put(w, 6, v.view_angles);
    if (v.view_punch)
        scalar(w, 7, *v.view_punch);
    put(w, 8, v.pawn_eye_position);
    put(w, 9, v.fov_override);
    put(w, 10, v.observer_mode);
    if (v.mouse_mapping)
        scalar(w, 12, *v.mouse_mapping);
    put(w, 13, v.crosshair_recoil);
    put(w, 14, v.camera_recoil_pawn);
    if (v.camera_recoil_path)
        w.uint32(15, static_cast<std::uint32_t>(*v.camera_recoil_path));
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
Writer world(const model::World& v) {
    Writer w;
    put(w, 1, v.freeze_period);
    put(w, 2, v.warmup);
    put(w, 3, v.match_started);
    put(w, 4, v.bomb_planted);
    put(w, 5, v.bomb_dropped);
    put(w, 6, v.round_time);
    put(w, 7, v.rounds_played);
    put(w, 8, v.round_win_status);
    put(w, 9, v.phase);
    put(w, 10, v.round_started_at);
    put(w, 11, v.minimap_min);
    put(w, 12, v.minimap_max);
    if (v.map_identity) {
        const auto& map = *v.map_identity;
        if (!map.generation || map.status == model::MapStatus::Unknown ||
            static_cast<std::uint32_t>(map.status) > static_cast<std::uint32_t>(model::MapStatus::Unsupported) ||
            (map.status == model::MapStatus::Available &&
             (!map.name || map.name->empty() || !map.resource_path || map.resource_path->empty())) ||
            (map.status != model::MapStatus::Available && (map.name || map.resource_path)))
            throw wire::Error("invalid map identity presence");
        Writer child;
        child.uint32(1, static_cast<std::uint32_t>(map.status));
        (void)child.fixed64(2, map.generation);
        put(child, 3, map.name);
        put(child, 4, map.resource_path);
        w.message(13, child);
    }
    return w;
}
} // namespace
wire::Bytes encode_snapshot(const model::Snapshot& v, const InstanceId& instance, const InstanceId& clock_domain) {
    if (!v.sequence || !v.epoch || !v.qpc_frequency || !v.interval.started_qpc ||
        v.interval.started_qpc > v.interval.read_finished_qpc || v.interval.read_finished_qpc > v.interval.finished_qpc)
        throw wire::Error("invalid capture identity or time interval");
    const auto& c = v.coverage;
    if (c.empty + c.invalid + c.unreadable + c.changed + c.unknown + c.classified != c.slots || !v.directory_verified)
        throw wire::Error("incomplete directory coverage");
    Writer w, identity, clock, interval, coverage;
    (void)identity.blob(1, instance);
    (void)identity.fixed64(2, v.epoch);
    (void)identity.fixed64(3, v.sequence);
    identity.uint32(4, v.process_id);
    identity.uint32(5, v.game_build);
    w.message(1, identity);
    (void)clock.blob(1, clock_domain);
    (void)clock.fixed64(2, v.qpc_frequency);
    w.message(2, clock);
    (void)interval.fixed64(1, v.interval.started_qpc);
    (void)interval.fixed64(2, v.interval.read_finished_qpc);
    (void)interval.fixed64(3, v.interval.finished_qpc);
    w.message(3, interval);
    (void)coverage.fixed64(1, c.slots);
    (void)coverage.fixed64(2, c.empty);
    (void)coverage.fixed64(3, c.invalid);
    (void)coverage.fixed64(4, c.unreadable);
    (void)coverage.fixed64(5, c.changed);
    (void)coverage.fixed64(6, c.unknown);
    (void)coverage.fixed64(7, c.classified);
    coverage.sint32(8, c.reported_highest_index);
    coverage.uint32(9, c.allocated_chunks);
    coverage.boolean(10, v.directory_verified);
    w.message(4, coverage);
    for (const auto& value : v.players)
        w.message(5, player(value));
    for (const auto& value : v.weapons)
        w.message(6, weapon(value));
    for (const auto& value : v.bombs)
        w.message(7, bomb(value));
    for (const auto& value : v.projectiles)
        w.message(8, projectile(value));
    for (const auto& value : v.world_objects)
        w.message(9, entity(value));
    w.message(10, local(v.local));
    w.message(11, world(v.world));
    return std::move(w.bytes);
}
} // namespace cs2::synchronization::v1
