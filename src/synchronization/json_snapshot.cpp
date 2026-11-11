#include "synchronization/json_snapshot.hpp"
#include <charconv>
#include <stdexcept>
#include <type_traits>
namespace cs2::synchronization {
namespace {
class Json {
  public:
    std::string result;
    void text(std::string_view value) {
        result += '"';
        constexpr char hex[] = "0123456789abcdef";
        for (unsigned char c : value) {
            if (c == '"' || c == '\\') {
                result += '\\';
                result += static_cast<char>(c);
            } else if (c < 32) {
                result += "\\u00";
                result += hex[c >> 4];
                result += hex[c & 15];
            } else
                result += static_cast<char>(c);
        }
        result += '"';
    }
    template <class T> void value(T number) {
        if constexpr (std::is_same_v<T, bool>)
            result += number ? "true" : "false";
        else {
            char buffer[64];
            auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), number);
            if (error != std::errc{})
                throw std::runtime_error("JSON number encoding failed");
            result.append(buffer, end);
        }
    }
    void value(const std::string& input) { text(input); }
    void value(model::EntityId input) { value(input.handle); }
    void value(model::Vec3 v) {
        result += '[';
        value(v.x);
        result += ',';
        value(v.y);
        result += ',';
        value(v.z);
        result += ']';
    }
    void value(model::Angles v) {
        result += '[';
        value(v.pitch);
        result += ',';
        value(v.yaw);
        result += ',';
        value(v.roll);
        result += ']';
    }
    void value(model::Quaternion v) {
        result += '[';
        value(v.x); result += ',';
        value(v.y); result += ',';
        value(v.z); result += ',';
        value(v.w); result += ']';
    }
    void value(model::GameTimePoint v) {
        result += '{';
        property("tick", v.tick);
        property("fraction", v.fraction);
        result += '}';
    }
    void value(const model::PredictableAimPunch& v) {
        result += '{';
        property("base_time", v.base_time);
        property("base_angle", v.base_angle);
        property("base_angular_velocity", v.base_angular_velocity);
        result += '}';
    }
    void value(const model::UnpredictableAimPunch& v) {
        result += '{';
        property("base_tick", v.base_tick);
        property("base_angle", v.base_angle);
        result += '}';
    }
    void value(const model::RecoilTimes& v) {
        result += '{';
        property("world_group", v.world_group);
        property("visual_policy", static_cast<std::uint32_t>(v.visual_policy));
        property("simulation", v.simulation);
        property("visual_predictable", v.visual_predictable);
        property("visual_unpredictable", v.visual_unpredictable);
        property("seconds_per_tick", 1.0f / 64.0f);
        property("native_context", v.native_context);
        result += '}';
    }
    void value(const model::AimPunchState& v) {
        result += '{';
        property("predictable", v.predictable);
        property("unpredictable", v.unpredictable);
        property("times", v.times);
        result += '}';
    }
    void value(const model::CameraViewPunchState& v) {
        result += '{';
        property("base_angle", v.base_angle);
        property("base_time", v.base_time);
        property("evaluation_time_seconds", v.evaluation_time_seconds);
        property("decay_exponent", v.decay_exponent);
        result += '}';
    }
    void value(const model::EyeAngleBasis& v) {
        result += '{';
        property("value", v.value);
        property("source", static_cast<std::uint32_t>(v.source));
        property("native_context", v.native_context);
        property("interpolation_parameter", v.interpolation_parameter);
        if (v.resolved_source)
            property("resolved_source", static_cast<std::uint32_t>(*v.resolved_source));
        property("source_entity", v.source_entity);
        property("delegation_depth", v.delegation_depth);
        result += '}';
    }
    void value(const model::MouseMapping& v) {
        result += '{';
        if (v.source)
            property("source", static_cast<std::uint32_t>(*v.source));
        property("sensitivity", v.sensitivity);
        property("vertical_scale", v.vertical_scale);
        property("yaw", v.yaw);
        property("pitch", v.pitch);
        property("zoom_ratio", v.zoom_ratio);
        property("inverted_y", v.inverted_y);
        property("effective_pitch", v.effective_pitch);
        property("pawn_fov_adjust", v.pawn_fov_adjust);
        property("pawn_mouse_sensitivity", v.pawn_mouse_sensitivity);
        property("yaw_degrees_per_delta", v.yaw_degrees_per_delta);
        property("pitch_degrees_per_delta", v.pitch_degrees_per_delta);
        result += '}';
    }
    template <class T, std::size_t N> void value(const std::array<T, N>& array) {
        result += '[';
        bool first = true;
        for (const auto& item : array) {
            if (!first)
                result += ',';
            first = false;
            value(item);
        }
        result += ']';
    }
    template <class T> void property(std::string_view name, const T& input) {
        if (result.back() != '{')
            result += ',';
        text(name);
        result += ':';
        value(input);
    }
    template <class T> void property(std::string_view name, const std::optional<T>& input) {
        if (input)
            property(name, *input);
    }
    void begin(std::string_view name) {
        if (result.back() != '{')
            result += ',';
        text(name);
        result += ":{";
    }
    template <class T, class F> void array(std::string_view name, const std::vector<T>& input, F write) {
        if (result.back() != '{')
            result += ',';
        text(name);
        result += ":[";
        bool first = true;
        for (const auto& item : input) {
            if (!first)
                result += ',';
            first = false;
            write(item);
        }
        result += ']';
    }
    void entity(const model::EntityState& e) {
        property("id", e.id);
        property("class", e.designer_name);
        property("position", e.transform.position);
        property("rotation", e.transform.rotation);
        property("scale", e.transform.scale);
        property("dormant", e.transform.dormant);
        property("velocity", e.world_velocity);
        property("team", e.team);
        property("health", e.health);
        property("max_health", e.max_health);
        property("life_state", e.life_state);
        property("flags", e.flags);
        property("owner", e.owner);
    }
    void weapon(const model::Weapon& w) {
        result += '{';
        entity(w.entity);
        property("definition_index", w.definition_index);
        property("custom_name", w.custom_name);
        property("clip", w.clip);
        property("secondary_clip", w.secondary_clip);
        property("reserve_ammo", w.reserve_ammo);
        property("paint_kit", w.paint_kit);
        property("wear", w.wear);
        property("stattrak", w.stattrak);
        property("accuracy_penalty", w.accuracy_penalty);
        property("recoil_index", w.recoil_index);
        property("reloading", w.reloading);
        property("silenced", w.silenced);
        property("burst_mode", w.burst_mode);
        if (w.accuracy_evaluation) {
            const auto& a = *w.accuracy_evaluation;
            begin("accuracy_evaluation");
            property("model_id", a.model_id);
            property("owner", a.owner);
            begin("inaccuracy");
            property("status", static_cast<unsigned>(a.inaccuracy.status));
            property("value", a.inaccuracy.value);
            result += '}';
            begin("spread");
            property("status", static_cast<unsigned>(a.spread.status));
            property("value", a.spread.value);
            result += '}';
            property("branch", static_cast<unsigned>(a.branch));
            property("movement", a.movement);
            property("airborne", a.airborne);
            property("turning", a.turning);
            property("lateral", a.lateral);
            property("penalty", a.penalty);
            property("mode_raw", a.mode_raw);
            property("accuracy_state_time_seconds", a.accuracy_state_time_seconds);
            property("started_qpc", a.started_qpc);
            property("read_finished_qpc", a.read_finished_qpc);
            result += '}';
        }
        result += '}';
    }
};
} // namespace
std::string snapshot_json(const model::Snapshot& s) {
    Json j;
    j.result.reserve(32768);
    j.result += '{';
    j.property("sequence", s.sequence);
    j.property("epoch", s.epoch);
    j.property("pid", s.process_id);
    j.property("game_build", s.game_build);
    j.property("qpc_frequency", s.qpc_frequency);
    j.begin("capture");
    j.property("started_qpc", s.interval.started_qpc);
    j.property("read_finished_qpc", s.interval.read_finished_qpc);
    j.property("finished_qpc", s.interval.finished_qpc);
    j.result += '}';
    j.begin("coverage");
    j.property("slots", s.coverage.slots);
    j.property("reported_highest_index", s.coverage.reported_highest_index);
    j.property("allocated_chunks", s.coverage.allocated_chunks);
    j.property("empty", s.coverage.empty);
    j.property("invalid", s.coverage.invalid);
    j.property("unreadable", s.coverage.unreadable);
    j.property("changed", s.coverage.changed);
    j.property("unknown", s.coverage.unknown);
    j.property("classified", s.coverage.classified);
    j.property("directory_verified", s.directory_verified);
    j.result += '}';
    j.begin("reads");
    j.property("calls", s.reads.calls);
    j.property("requested", s.reads.requested);
    j.property("received", s.reads.received);
    j.property("failures", s.reads.failures);
    j.property("hypercall_single", s.reads.hypercall_single);
    j.property("hypercall_batch", s.reads.hypercall_batch);
    j.property("batch_items", s.reads.batch_items);
    j.property("batch_errors", s.reads.batch_errors);
    j.property("batch_item_splits", s.reads.batch_item_splits);
    j.property("batch_byte_splits", s.reads.batch_byte_splits);
    j.result += '}';
    j.array("players", s.players, [&](const model::Player& p) {
        j.result += '{';
        j.property("controller", p.controller);
        j.property("name", p.name);
        j.property("sanitized_name", p.sanitized_name);
        j.property("steam_id", p.steam_id);
        j.property("team", p.team);
        j.property("connection_state", p.connection_state);
        j.property("local", p.local);
        j.property("ping", p.ping);
        j.property("score", p.score);
        j.property("money", p.money);
        j.property("bot_difficulty", p.bot_difficulty);
        j.property("controller_reports_alive", p.controller_reports_alive);
        j.property("controller_reported_health", p.controller_reported_health);
        j.property("controller_reported_armor", p.controller_reported_armor);
        j.property("player_pawn", p.player_pawn);
        j.property("observer_pawn", p.observer_pawn);
        if (p.body) {
            const auto& b = *p.body;
            j.begin("body");
            j.entity(b.entity);
            j.property("armor", b.armor);
            j.property("shots_fired", b.shots_fired);
            j.property("scoped", b.scoped);
            j.property("defusing", b.defusing);
            j.property("helmet", b.helmet);
            j.property("defuser", b.defuser);
            j.property("spotted", b.spotted);
            j.property("spotted_by", b.spotted_by);
            j.property("flash_duration", b.flash_duration);
            j.property("flash_max_alpha", b.flash_max_alpha);
            j.property("eye_angles", b.eye_angles);
            j.property("input_eye_angles", b.input_eye_angles);
            j.property("eye_basis", b.eye_basis);
            j.property("aim_punch", b.aim_punch);
            j.property("view_offset", b.view_offset);
            j.property("location", b.location);
            j.property("active_weapon", b.active_weapon);
            if (b.weapon) {
                if (j.result.back() != '{')
                    j.result += ',';
                j.text("weapon");
                j.result += ':';
                j.weapon(*b.weapon);
            }
            j.array("equipped_weapons", b.equipped_weapons, [&](auto id) { j.value(id); });
            if (b.skeleton) {
                j.begin("skeleton");
                j.property("status", static_cast<std::uint32_t>(b.skeleton->status));
                j.property("model_name", b.skeleton->model_name);
                j.property("profile_id", b.skeleton->profile_id);
                j.property("native_hitbox_set_available", b.skeleton->native_hitbox_set_available);
                j.array("points", b.skeleton->points, [&](const auto& bone) {
                    j.result += '{';
                    j.property("index", bone.index);
                    j.property("position", bone.position);
                    if (bone.role)
                        j.property("role", static_cast<std::uint32_t>(*bone.role));
                    if (bone.world_pose) {
                        j.property("world_rotation", bone.world_pose->rotation);
                        j.property("world_uniform_scale", bone.world_pose->uniform_scale);
                    }
                    j.result += '}';
                });
                j.array("links", b.skeleton->links, [&](const auto& edge) {
                    j.result += '{';
                    j.property("parent_index", edge.parent_index);
                    j.property("child_index", edge.child_index);
                    j.result += '}';
                });
                j.result += '}';
            }
            j.result += '}';
        }
        j.result += '}';
    });
    j.array("weapons", s.weapons, [&](const auto& w) { j.weapon(w); });
    j.array("bombs", s.bombs, [&](const model::Bomb& b) {
        j.result += '{';
        j.entity(b.entity);
        j.property("ticking", b.ticking);
        j.property("defused", b.defused);
        j.property("being_defused", b.being_defused);
        j.property("exploded", b.exploded);
        j.property("site", b.site);
        j.property("explode_at", b.explode_at);
        j.property("timer_length", b.timer_length);
        j.property("defuse_at", b.defuse_at);
        j.property("defuse_length", b.defuse_length);
        j.property("defuser", b.defuser);
        j.result += '}';
    });
    j.array("projectiles", s.projectiles, [&](const model::Projectile& p) {
        j.result += '{';
        j.entity(p.entity);
        j.property("initial_position", p.initial_position);
        j.property("initial_velocity", p.initial_velocity);
        j.property("bounces", p.bounces);
        j.property("explode_tick", p.explode_tick);
        j.property("exploded", p.exploded);
        j.property("spawn_time", p.spawn_time);
        j.property("smoke_tick", p.smoke_tick);
        j.property("smoke_started", p.smoke_started);
        j.result += '}';
    });
    j.array("world_objects", s.world_objects, [&](const auto& e) {
        j.result += '{';
        j.entity(e);
        j.result += '}';
    });
    const auto& l = s.local;
    j.begin("local");
    j.property("controller", l.controller);
    j.property("pawn", l.pawn);
    j.property("observer_target", l.observer_target);
    j.property("view_entity", l.view_entity);
    j.property("view_matrix", l.view_matrix);
    j.property("view_angles", l.view_angles);
    j.property("view_punch", l.view_punch);
    j.property("pawn_eye_position", l.pawn_eye_position);
    j.property("fov_override", l.fov_override);
    j.property("observer_mode", l.observer_mode);
    j.property("mouse_mapping", l.mouse_mapping);
    j.property("crosshair_recoil", l.crosshair_recoil);
    j.property("camera_recoil_pawn", l.camera_recoil_pawn);
    if (l.camera_recoil_path)
        j.property("camera_recoil_path", static_cast<std::uint32_t>(*l.camera_recoil_path));
    j.result += '}';
    const auto& w = s.world;
    j.begin("world");
    j.property("freeze_period", w.freeze_period);
    j.property("warmup", w.warmup);
    j.property("match_started", w.match_started);
    j.property("bomb_planted", w.bomb_planted);
    j.property("bomb_dropped", w.bomb_dropped);
    j.property("round_time", w.round_time);
    j.property("rounds_played", w.rounds_played);
    j.property("round_win_status", w.round_win_status);
    j.property("phase", w.phase);
    j.property("round_started_at", w.round_started_at);
    j.property("minimap_min", w.minimap_min);
    j.property("minimap_max", w.minimap_max);
    if (w.map_identity) {
        const auto& map = *w.map_identity;
        j.begin("map_identity");
        j.property("status", static_cast<std::uint32_t>(map.status));
        j.property("generation", map.generation);
        j.property("name", map.name);
        j.property("resource_path", map.resource_path);
        j.result += '}';
    }
    j.result += "}}";
    return std::move(j.result);
}
} // namespace cs2::synchronization
