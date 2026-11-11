#include "game/cs2/sampling/skeleton.hpp"
#include "game/cs2/entity/layout.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "game/cs2/skeleton/profile.hpp"
#include "game/cs2/skeleton/pose.hpp"
#include "memory/block_reader.hpp"
#include <algorithm>

namespace cs2::game {
namespace {
struct Header {
    memory::Address bones, model, name;
    bool operator==(const Header&) const = default;
};
std::optional<Header> read_header(memory::IReadChannel& reader, memory::Address state) {
    constexpr auto begin = layout::model_bone_array;
    constexpr auto model_offset = static_cast<std::size_t>(offsets::schema::CModelState::m_hModel);
    constexpr auto name_offset = static_cast<std::size_t>(offsets::schema::CModelState::m_ModelName);
    static_assert(model_offset >= begin && name_offset >= begin);
    constexpr auto bytes = std::max(model_offset, name_offset) + sizeof(memory::Address) - begin;
    static_assert(bytes <= 128, "Review the model-header cluster when its layout changes");
    memory::Block block;
    block.read(reader, state + begin, bytes);
    const auto bones = block.get<memory::Address>(0), model = block.get<memory::Address>(model_offset - begin),
               name = block.get<memory::Address>(name_offset - begin);
    if (!bones || !model || !name)
        return {};
    return Header{*bones, *model, *name};
}
} // namespace
model::Skeleton sample_skeleton(memory::IReadChannel& reader, memory::Address pawn,
                               std::optional<memory::Address> scene) {
    model::Skeleton result;
    if (!scene || !memory::user_range(*scene))
        return result;
    const auto owner_offset = offsets::schema::CGameSceneNode::m_pOwner;
    if (memory::read_value<memory::Address>(reader, *scene + owner_offset) != std::optional(pawn))
        return result;
    const auto state = *scene + offsets::schema::CSkeletonInstance::m_modelState;
    const auto header = read_header(reader, state);
    if (!header || !memory::user_range(header->model) || !memory::user_range(header->name))
        return result;
    const auto name = memory::read_string(reader, header->name, 512);
    if (!name)
        return result;
    result.model_name = skeleton::canonical_model_name(*name);
    const auto* profile =
        result.model_name ? skeleton::find_profile(*result.model_name, offsets::expected_build()) : nullptr;
    result.status = model::SkeletonStatus::UnknownModel;
    std::optional<memory::Address> loaded_model;
    std::optional<std::uint64_t> mesh_mask;
    if (profile) {
        result.profile_id = std::string(profile->id);
        result.status = model::SkeletonStatus::SourceUnavailable;
        loaded_model = memory::read_value<memory::Address>(reader, header->model);
        if (loaded_model && memory::user_range(*loaded_model))
            mesh_mask = memory::read_value<std::uint64_t>(reader, state + offsets::schema::CModelState::m_MeshGroupMask);
        const auto last = std::max_element(profile->bones.begin(), profile->bones.end(),
                                           [](auto a, auto b) { return a.index < b.index; });
        const auto bytes = (static_cast<std::size_t>(last->index) + 1) * layout::bone_stride;
        if (memory::user_range(header->bones, bytes)) {
            memory::Block positions;
            positions.read(reader, header->bones, bytes);
            result.points.reserve(profile->bones.size());
            for (const auto& bone : profile->bones) {
                const auto position = positions.get<model::Vec3>(bone.index * layout::bone_stride);
                if (position && model::finite(*position)) {
                    model::BonePoint point{bone.index, *position, bone.role};
                    // 沿用本批同一份骨数组拷贝，读取额外字段不再发起任何远程调用。
                    // 尾部短读或旋转坏值只撤销可选姿态，已取得的位置和连线继续保留。
                    if (const auto transform = positions.get<skeleton::NativeBoneTransform>(bone.index * layout::bone_stride))
                        point.world_pose = skeleton::world_pose(*transform);
                    result.points.push_back(std::move(point));
                }
            }
            if (!result.points.empty()) {
                result.status = result.points.size() == profile->bones.size() ? model::SkeletonStatus::VerifiedComplete
                                                                              : model::SkeletonStatus::VerifiedPartial;
                const auto present = [&](auto index) {
                    return std::any_of(result.points.begin(), result.points.end(),
                                       [&](const auto& point) { return point.index == index; });
                };
                for (const auto& bone : profile->bones)
                    if (present(bone.index) && present(bone.parent_index))
                        result.links.push_back({bone.parent_index, bone.index});
            }
        }
    }
    // 未知模型不读取猜测的骨点。任何来源变化撤销整个骨骼组，不影响人物其他已读取属性。
    if (read_header(reader, state) != header ||
        memory::read_value<memory::Address>(reader, *scene + owner_offset) != std::optional(pawn) ||
        memory::read_value<memory::Address>(reader, pawn + offsets::schema::C_BaseEntity::m_pGameSceneNode) != scene)
        return {};
    if (profile) {
        // CStrongHandle绑定地址可能保持不变而加载对象已切换；只撤销新姿态，不退化旧骨点。
        const auto closing_model = memory::read_value<memory::Address>(reader, header->model);
        if (!loaded_model || !memory::user_range(*loaded_model) || closing_model != loaded_model) {
            for (auto& point : result.points)
                point.world_pose.reset();
        } else if (mesh_mask) {
            const auto closing_mask =
                memory::read_value<std::uint64_t>(reader, state + offsets::schema::CModelState::m_MeshGroupMask);
            const auto eligible = skeleton::hitbox_group_mask(*result.model_name, offsets::expected_build());
            if (eligible && closing_mask == mesh_mask)
                result.native_hitbox_set_available = (*mesh_mask & *eligible) != 0;
        }
    }
    return result;
}
} // namespace cs2::game
