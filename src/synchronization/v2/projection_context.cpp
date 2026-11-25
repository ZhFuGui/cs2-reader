#include "model/local.hpp"
#include "model/world.hpp"
#include "synchronization/v2/projection_detail.hpp"

namespace cs2::synchronization::v2::projection {
namespace {
void write_view_punch(Writer& w, unsigned f, const model::CameraViewPunchState& state) {
    Writer child;
    scalar(child, 1, state.base_angle);
    scalar(child, 2, state.base_time);
    put(child, 3, state.evaluation_time_seconds);
    put(child, 4, state.decay_exponent);
    w.message(f, child);
}
void write_mouse_mapping(Writer& w, unsigned f, const model::MouseMapping& state) {
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
} // namespace

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
        write_view_punch(w, 7, *v.view_punch);
    put(w, 8, v.pawn_eye_position);
    put(w, 9, v.fov_override);
    put(w, 10, v.observer_mode);
    if (v.mouse_mapping)
        write_mouse_mapping(w, 12, *v.mouse_mapping);
    put(w, 13, v.crosshair_recoil);
    put(w, 14, v.camera_recoil_pawn);
    if (v.camera_recoil_path)
        w.uint32(15, static_cast<std::uint32_t>(*v.camera_recoil_path));
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
} // namespace cs2::synchronization::v2::projection
