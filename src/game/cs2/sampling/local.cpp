#include "game/cs2/sampling/local.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "game/cs2/sampling/camera_recoil.hpp"
#include "game/cs2/sampling/common.hpp"
#include "game/cs2/sampling/input.hpp"
#include <algorithm>
namespace cs2::game {
model::Local sample_local(memory::IReadChannel& reader, const Context& context, const Directory& directory) {
    model::Local result;
    const auto client = context.client.base;
    auto matrix = memory::read_value<model::ViewMatrix>(reader, client + offsets::client::dwViewMatrix);
    if (matrix && std::all_of(matrix->begin(), matrix->end(), [](float v) { return std::isfinite(v); }))
        result.view_matrix = matrix;
    const auto angle = memory::read_value<model::Angles>(reader, client + offsets::client::dwViewAngles);
    if (angle && model::finite(*angle))
        result.view_angles = angle;
    result.crosshair_recoil = sample_crosshair_recoil(reader, context);
    const auto controller =
        memory::read_value<memory::Address>(reader, client + offsets::client::dwLocalPlayerController);
    const auto* controller_ref = controller ? directory.find_address(*controller) : nullptr;
    std::optional<std::uint32_t> pawn;
    if (controller_ref && controller_ref->kind == EntityKind::Controller && directory.verify(reader, *controller_ref)) {
        result.controller = model::EntityId{controller_ref->handle};
        // 与原生slot0 GetPawn一致；dwLocalPlayerPawn可能在死亡后仍保留原Player身体。
        pawn = memory::read_value<std::uint32_t>(reader, controller_ref->address +
                                                             offsets::schema::CBasePlayerController::m_hPawn);
    } else
        controller_ref = nullptr;
    const auto* ref = pawn ? directory.resolve(*pawn) : nullptr;
    if (ref && ref->kind != EntityKind::PlayerPawn && ref->kind != EntityKind::ObserverPawn &&
        ref->designer_name != "cs_observer_pawn")
        ref = nullptr;
    result.mouse_mapping = sample_mouse_mapping(reader, context, directory, ref);
    const auto clear_pawn = [&] {
        result.pawn.reset();
        result.observer_target.reset();
        result.observer_mode.reset();
        result.view_entity.reset();
        result.fov_override.reset();
        result.view_punch.reset();
        result.camera_recoil_pawn.reset();
        result.camera_recoil_path.reset();
        result.pawn_eye_position.reset();
        result.mouse_mapping.reset();
    };
    if (ref &&
        (ref->kind == EntityKind::PlayerPawn || ref->kind == EntityKind::ObserverPawn ||
         ref->designer_name == "cs_observer_pawn") &&
        directory.verify(reader, *ref)) {
        result.pawn = model::EntityId{ref->handle};
        namespace base = offsets::schema::C_BasePlayerPawn;
        using memory::field;
        const memory::Fields fields(reader, ref->address, entity_fields(),
                                    {field<memory::Address>(base::m_pCameraServices),
                                     field<memory::Address>(base::m_pObserverServices),
                                     field<model::Vec3>(offsets::schema::C_BaseModelEntity::m_vecViewOffset)});
        if (const auto camera = fields.get<memory::Address>(base::m_pCameraServices);
            camera && memory::user_range(*camera)) {
            namespace service = offsets::schema::CPlayer_CameraServices;
            namespace fov = offsets::schema::CCSPlayerBase_CameraServices;
            const memory::Fields values(
                reader, *camera, {field<std::uint32_t>(service::m_hViewEntity), field<std::uint32_t>(fov::m_iFOV)});
            if (memory::read_value<memory::Address>(reader, ref->address + base::m_pCameraServices) == camera) {
                result.fov_override = values.get<std::uint32_t>(fov::m_iFOV);
                if (const auto target = values.get<std::uint32_t>(service::m_hViewEntity);
                    target && *target != UINT32_MAX)
                    result.view_entity = model::EntityId{*target};
            }
        }
        if (const auto observer = fields.get<memory::Address>(base::m_pObserverServices);
            observer && memory::user_range(*observer)) {
            namespace service = offsets::schema::CPlayer_ObserverServices;
            const memory::Fields values(
                reader, *observer,
                {field<std::uint8_t>(service::m_iObserverMode), field<std::uint32_t>(service::m_hObserverTarget)});
            if (memory::read_value<memory::Address>(reader, ref->address + base::m_pObserverServices) == observer) {
                result.observer_mode = values.get<std::uint8_t>(service::m_iObserverMode);
                if (const auto target = values.get<std::uint32_t>(service::m_hObserverTarget);
                    target && *target != UINT32_MAX)
                    result.observer_target = model::EntityId{*target};
            }
        }
        if (const auto camera = sample_camera_recoil(reader, context, directory, *ref)) {
            result.camera_recoil_pawn = camera->pawn;
            result.camera_recoil_path = camera->path;
            result.view_punch = camera->view_punch;
        }
        const auto entity = sample_common(reader, *ref, fields);
        const auto view = vector3(fields, offsets::schema::C_BaseModelEntity::m_vecViewOffset);
        if (entity.transform.position && view) {
            const auto origin = *entity.transform.position;
            result.pawn_eye_position = model::Vec3{origin.x + view->x, origin.y + view->y, origin.z + view->z};
        }
        if (!directory.verify(reader, *ref) || !controller_ref ||
            memory::read_value<std::uint32_t>(reader, controller_ref->address +
                                                          offsets::schema::CBasePlayerController::m_hPawn) != pawn)
            clear_pawn();
    }
    if (memory::read_value<memory::Address>(reader, client + offsets::client::dwLocalPlayerController) != controller ||
        (controller_ref && !directory.verify(reader, *controller_ref))) {
        result.controller.reset();
        clear_pawn();
    }
    return result;
}
} // namespace cs2::game
