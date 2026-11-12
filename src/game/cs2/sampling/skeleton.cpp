#include "game/cs2/sampling/skeleton.hpp"
#include "game/cs2/entity/layout.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "game/cs2/skeleton/pose.hpp"
#include "game/cs2/skeleton/profile.hpp"
#include "memory/block_reader.hpp"
#include <algorithm>

namespace cs2::game {
namespace {
constexpr auto state_offset = offsets::schema::CSkeletonInstance::m_modelState;
constexpr auto owner_offset = offsets::schema::CGameSceneNode::m_pOwner;
constexpr auto bones_offset = state_offset + layout::model_bone_array;
constexpr auto model_offset = state_offset + offsets::schema::CModelState::m_hModel;
constexpr auto name_offset = state_offset + offsets::schema::CModelState::m_ModelName;
constexpr auto mask_offset = state_offset + offsets::schema::CModelState::m_MeshGroupMask;
constexpr auto header_bytes = std::max<std::size_t>(model_offset, name_offset) + 8 - bones_offset;
static_assert(model_offset >= bones_offset && name_offset >= bones_offset && header_bytes <= 128);
struct Header {
    memory::Address bones, model, name;
    bool operator==(const Header&) const = default;
};
std::optional<Header> header(const memory::Block& block) {
    const auto bones = block.get<memory::Address>(0), model = block.get<memory::Address>(model_offset - bones_offset),
               name = block.get<memory::Address>(name_offset - bones_offset);
    if (!bones || !model || !name)
        return {};
    return Header{*bones, *model, *name};
}
using Ticket = memory::ReadPlan::Ticket;
struct Work {
    explicit Work(const SkeletonRead& read) : request(read) {}
    const SkeletonRead& request;
    memory::Block opening, closing, name, positions;
    std::optional<Header> source;
    const skeleton::Profile* profile = nullptr;
    bool named = false;
    memory::Address owner{}, closing_owner{}, closing_loaded{}, closing_scene{};
    std::uint64_t mask{}, closing_mask{};
    Ticket owner_ticket{}, name_pointer{}, model_pointer{}, loaded_pointer{}, bone_pointer{}, mask_ticket{};
    Ticket closing_owner_ticket{}, scene_ticket{};
    std::optional<Ticket> closing_model_ticket, closing_mask_ticket;
};
void project_positions(Work& work) {
    auto& result = work.request.result;
    const auto& profile = *work.profile;
    result.points.reserve(profile.bones.size());
    for (const auto& bone : profile.bones) {
        const auto position = work.positions.get<model::Vec3>(bone.index * layout::bone_stride);
        if (!position || !model::finite(*position))
            continue;
        model::BonePoint point{bone.index, *position, bone.role};
        if (const auto transform = work.positions.get<skeleton::NativeBoneTransform>(bone.index * layout::bone_stride))
            point.world_pose = skeleton::world_pose(*transform);
        result.points.push_back(std::move(point));
    }
    if (result.points.empty())
        return;
    result.status = result.points.size() == profile.bones.size() ? model::SkeletonStatus::VerifiedComplete
                                                                 : model::SkeletonStatus::VerifiedPartial;
    const auto present = [&](auto index) {
        return std::any_of(result.points.begin(), result.points.end(),
                           [&](const auto& point) { return point.index == index; });
    };
    for (const auto& bone : profile.bones)
        if (present(bone.index) && present(bone.parent_index))
            result.links.push_back({bone.parent_index, bone.index});
}
} // namespace
model::Skeleton sample_skeleton(memory::IReadChannel& reader, memory::Address pawn,
                                std::optional<memory::Address> scene) {
    model::Skeleton result;
    const SkeletonRead request{pawn, scene, result};
    sample_skeletons(reader, std::span(&request, 1));
    return result;
}
void sample_skeletons(memory::IReadChannel& reader, std::span<const SkeletonRead> requests) {
    std::vector<Work> work;
    work.reserve(requests.size());
    for (const auto& request : requests) {
        request.result = {};
        constexpr auto extent =
            std::max<std::size_t>({owner_offset, bones_offset, model_offset, name_offset, mask_offset}) + 8;
        if (request.scene && memory::user_range(*request.scene, extent))
            work.emplace_back(request);
    }
    memory::ReadPlan opening;
    std::vector<memory::Block::PlannedRead> blocks;
    for (auto& item : work) {
        const auto scene = *item.request.scene;
        item.owner_ticket = opening.add(scene + owner_offset, item.owner);
        blocks.push_back({item.opening, item.opening.schedule(opening, scene + bones_offset, header_bytes)});
        item.name_pointer = opening.pointer(scene + name_offset);
        blocks.push_back({item.name, item.name.schedule_from(opening, item.name_pointer, 0, 512)});
    }
    opening.execute(reader);
    memory::Block::finish_many(reader, opening, blocks);
    memory::ReadPlan data;
    blocks.clear();
    for (auto& item : work) {
        if (!opening.complete(item.owner_ticket) || item.owner != item.request.pawn)
            continue;
        item.source = header(item.opening);
        if (!item.source || !memory::user_range(item.source->model) || !memory::user_range(item.source->name) ||
            opening.pointer_value(item.name_pointer) != std::optional(item.source->name))
            continue;
        const auto name = item.name.text(0, 512);
        if (!name)
            continue;
        item.named = true;
        auto& result = item.request.result;
        result.model_name = skeleton::canonical_model_name(*name);
        item.profile =
            result.model_name ? skeleton::find_profile(*result.model_name, offsets::expected_build()) : nullptr;
        result.status = model::SkeletonStatus::UnknownModel;
        if (!item.profile)
            continue; // 未知模型不读取猜测的骨点。
        result.profile_id = std::string(item.profile->id);
        result.status = model::SkeletonStatus::SourceUnavailable;
        // 名称完成后才选择已核验骨架；模型加载对象和骨数组在同一依赖计划中获取。
        item.model_pointer = data.pointer(*item.request.scene + model_offset);
        item.loaded_pointer = data.pointer_from(item.model_pointer, 0);
        item.mask_ticket = data.add(*item.request.scene + mask_offset, item.mask);
        const auto last = std::max_element(item.profile->bones.begin(), item.profile->bones.end(),
                                           [](auto a, auto b) { return a.index < b.index; });
        const auto bytes = (static_cast<std::size_t>(last->index) + 1) * layout::bone_stride;
        item.bone_pointer = data.pointer(*item.request.scene + bones_offset);
        blocks.push_back({item.positions, item.positions.schedule_from(data, item.bone_pointer, 0, bytes)});
    }
    data.execute(reader);
    memory::Block::finish_many(reader, data, blocks);
    for (auto& item : work)
        if (item.named && item.profile)
            project_positions(item);

    memory::ReadPlan closing;
    blocks.clear();
    for (auto& item : work) {
        if (!item.named)
            continue;
        const auto scene = *item.request.scene;
        blocks.push_back({item.closing, item.closing.schedule(closing, scene + bones_offset, header_bytes)});
        item.closing_owner_ticket = closing.add(scene + owner_offset, item.closing_owner);
        item.scene_ticket =
            closing.add(item.request.pawn + offsets::schema::C_BaseEntity::m_pGameSceneNode, item.closing_scene);
        if (item.profile) {
            item.closing_model_ticket = closing.add(item.source->model, item.closing_loaded);
            if (const auto loaded = data.pointer_value(item.loaded_pointer);
                loaded && memory::user_range(*loaded) && data.complete(item.mask_ticket))
                item.closing_mask_ticket = closing.add(scene + mask_offset, item.closing_mask);
        }
    }
    closing.execute(reader);
    memory::Block::finish_many(reader, closing, blocks);
    for (const auto& item : work) {
        if (!item.named)
            continue;
        auto& result = item.request.result;
        const auto model = item.profile ? data.pointer_value(item.model_pointer) : std::nullopt;
        const auto bones = item.profile ? data.pointer_value(item.bone_pointer) : std::nullopt;
        if (header(item.closing) != item.source || !closing.complete(item.closing_owner_ticket) ||
            item.closing_owner != item.request.pawn || !closing.complete(item.scene_ticket) ||
            item.closing_scene != *item.request.scene || (model && *model != item.source->model) ||
            (bones && *bones != item.source->bones)) {
            result = {};
            continue;
        }
        if (!item.profile)
            continue;
        const auto loaded = data.pointer_value(item.loaded_pointer);
        if (!loaded || !memory::user_range(*loaded) || !closing.complete(*item.closing_model_ticket) ||
            item.closing_loaded != *loaded) {
            for (auto& point : result.points)
                point.world_pose.reset();
        } else if (item.closing_mask_ticket && closing.complete(*item.closing_mask_ticket) &&
                   item.closing_mask == item.mask) {
            if (const auto eligible = skeleton::hitbox_group_mask(*result.model_name, offsets::expected_build()))
                result.native_hitbox_set_available = (item.mask & *eligible) != 0;
        }
    }
}
} // namespace cs2::game
