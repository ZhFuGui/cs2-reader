#include "game/cs2/sampling/camera_recoil.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "game/cs2/recoil/profile.hpp"
#include "game/cs2/sampling/recoil.hpp"
#include "memory/block_reader.hpp"

namespace cs2::game {
std::optional<CameraRecoilSample> sample_camera_recoil(memory::IReadChannel& reader, const Context& context,
                                                       const Directory& directory, const EntityRef& local_pawn) {
    if (context.build != recoil_profile::build || !directory.verify(reader, local_pawn))
        return {};
    namespace base = offsets::schema::C_BasePlayerPawn;
    namespace observer_fields = offsets::schema::CPlayer_ObserverServices;
    using memory::Address;
    const auto alive = [&](const EntityRef& pawn) {
        return memory::read_value<std::uint8_t>(reader, pawn.address + offsets::schema::C_BaseEntity::m_lifeState) ==
               std::optional(std::uint8_t(0));
    };
    const EntityRef* source = nullptr;
    std::optional<Address> observer;
    std::optional<Address> observer_table;
    std::optional<std::uint8_t> mode;
    std::optional<std::uint32_t> target;
    auto path = model::CameraRecoilPath::Player;
    if (local_pawn.kind == EntityKind::PlayerPawn && alive(local_pawn)) {
        source = &local_pawn;
    } else {
        observer = memory::read_value<Address>(reader, local_pawn.address + base::m_pObserverServices);
        if (!observer || !memory::user_range(*observer))
            return {};
        observer_table = memory::read_value<Address>(reader, *observer);
        if (!observer_table || !memory::user_range(*observer_table) ||
            memory::read_value<Address>(reader, *observer_table + 0xf0) !=
                std::optional(context.client.base + recoil_profile::observer_mode_function) ||
            memory::read_value<Address>(reader, *observer_table + 0x100) !=
                std::optional(context.client.base + recoil_profile::observer_target_function) ||
            memory::read_value<Address>(reader, *observer_table + 0x110) !=
                std::optional(context.client.base + recoil_profile::observer_dispatch_function) ||
            memory::read_value<Address>(reader, *observer_table + 0x1a8) !=
                std::optional(context.client.base + recoil_profile::observer_in_eye_function))
            return {};
        const memory::Fields values(reader, *observer,
                                    {memory::field<std::uint8_t>(observer_fields::m_iObserverMode),
                                     memory::field<std::uint32_t>(observer_fields::m_hObserverTarget)});
        mode = values.get<std::uint8_t>(observer_fields::m_iObserverMode);
        target = values.get<std::uint32_t>(observer_fields::m_hObserverTarget);
        // build14192的原生getter/dispatch中2才进入in-eye分支，不套用旧Source1枚举。
        if (!mode || *mode != 2 || !target)
            return {};
        source = directory.resolve(*target);
        if (!source || source->kind != EntityKind::PlayerPawn || !alive(*source))
            return {};
        // 目标活着时原生getter保留2；死亡目标还受额外规则影响，不能伪造成普通第一人称。
        path = model::CameraRecoilPath::ObservedPlayer;
    }
    if (!directory.verify(reader, *source))
        return {};
    const auto camera = memory::read_value<Address>(reader, source->address + base::m_pCameraServices);
    if (!camera || !memory::user_range(*camera))
        return {};
    const auto table = memory::read_value<Address>(reader, *camera);
    if (!table || !memory::user_range(*table) ||
        memory::read_value<Address>(reader, *table + 0x110) !=
            std::optional(context.client.base + recoil_profile::player_camera_function))
        return {};
    CameraRecoilSample result{
        {source->handle}, path, sample_camera_view_punch(reader, context, directory, *source, *camera)};
    if (!alive(*source) || !directory.verify(reader, *source) || !directory.verify(reader, local_pawn) ||
        memory::read_value<Address>(reader, source->address + base::m_pCameraServices) != camera ||
        memory::read_value<Address>(reader, *camera) != table)
        return {};
    if (observer &&
        (memory::read_value<Address>(reader, local_pawn.address + base::m_pObserverServices) != observer ||
         memory::read_value<Address>(reader, *observer) != observer_table ||
         memory::read_value<std::uint8_t>(reader, *observer + observer_fields::m_iObserverMode) != mode ||
         memory::read_value<std::uint32_t>(reader, *observer + observer_fields::m_hObserverTarget) != target))
        return {};
    return result;
}
} // namespace cs2::game
