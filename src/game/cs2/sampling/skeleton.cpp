#include "game/cs2/sampling/skeleton.hpp"
#include "game/cs2/entity/layout.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "game/cs2/skeleton/pose.hpp"
#include "game/cs2/skeleton/profile.hpp"
#include "memory/block_reader.hpp"
#include "memory/read_plan.hpp"
#include <algorithm>

namespace cs2::game {
namespace {
constexpr auto state_offset = offsets::schema::CSkeletonInstance::m_modelState;
constexpr auto owner_offset = offsets::schema::CGameSceneNode::m_pOwner;
constexpr auto bones_offset = state_offset + layout::model_bone_array;
constexpr auto model_offset = state_offset + offsets::schema::CModelState::m_hModel;
constexpr auto name_offset = state_offset + offsets::schema::CModelState::m_ModelName;
constexpr auto mask_offset = state_offset + offsets::schema::CModelState::m_MeshGroupMask;
constexpr memory::Field sources[]{
    memory::field<memory::Address>(owner_offset), memory::field<memory::Address>(bones_offset),
    memory::field<memory::Address>(model_offset), memory::field<memory::Address>(name_offset)};
struct Header {
    memory::Address bones, model, name;
    bool operator==(const Header&) const = default;
};
std::optional<Header> header(const memory::Fields& fields) {
    const auto bones = fields.get<memory::Address>(bones_offset), model = fields.get<memory::Address>(model_offset),
               name = fields.get<memory::Address>(name_offset);
    if (!bones || !model || !name)
        return {};
    return Header{*bones, *model, *name};
}
struct Work {
    explicit Work(const SkeletonRead& read)
        : request(read), opening(*read.scene, sources), closing(*read.scene, sources) {}
    const SkeletonRead& request;
    memory::Fields opening, closing;
    memory::Block name, positions;
    std::optional<Header> source;
    const skeleton::Profile* profile = nullptr;
    bool named = false;
    memory::Address loaded{}, closing_loaded{}, closing_scene{};
    std::uint64_t mask{}, closing_mask{};
    std::optional<memory::ReadPlan::Ticket> model_ticket, mask_ticket, closing_model_ticket, closing_mask_ticket;
    memory::ReadPlan::Ticket scene_ticket{};
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
        // 位置与姿态使用同一份骨数组；可选尾部或旋转失败不撤销已成功的位置。
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
    std::vector<memory::Fields*> fields;
    for (auto& item : work)
        fields.push_back(&item.opening);
    memory::Fields::read_many(reader, fields);
    std::vector<memory::Block::Read> blocks;
    for (auto& item : work) {
        if (item.opening.get<memory::Address>(owner_offset) != std::optional(item.request.pawn))
            continue;
        item.source = header(item.opening);
        if (item.source && memory::user_range(item.source->model) && memory::user_range(item.source->name))
            blocks.push_back({item.name, item.source->name, 512});
        else
            item.source.reset();
    }
    memory::Block::read_many(reader, blocks);
    memory::ReadPlan model_reads;
    blocks.clear();
    for (auto& item : work) {
        if (!item.source)
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
        item.model_ticket = model_reads.add(item.source->model, item.loaded);
        item.mask_ticket = model_reads.add(*item.request.scene + mask_offset, item.mask);
        const auto last = std::max_element(item.profile->bones.begin(), item.profile->bones.end(),
                                           [](auto a, auto b) { return a.index < b.index; });
        const auto bytes = (static_cast<std::size_t>(last->index) + 1) * layout::bone_stride;
        if (memory::user_range(item.source->bones, bytes))
            blocks.push_back({item.positions, item.source->bones, bytes});
    }
    model_reads.execute(reader);
    memory::Block::read_many(reader, blocks);
    for (auto& item : work)
        if (item.named && item.profile)
            project_positions(item);

    fields.clear();
    memory::ReadPlan closing;
    for (auto& item : work) {
        if (!item.named)
            continue;
        fields.push_back(&item.closing);
        item.scene_ticket =
            closing.add(item.request.pawn + offsets::schema::C_BaseEntity::m_pGameSceneNode, item.closing_scene);
        if (item.profile) {
            item.closing_model_ticket = closing.add(item.source->model, item.closing_loaded);
            if (model_reads.complete(*item.model_ticket) && memory::user_range(item.loaded) &&
                model_reads.complete(*item.mask_ticket))
                item.closing_mask_ticket = closing.add(*item.request.scene + mask_offset, item.closing_mask);
        }
    }
    memory::Fields::read_many(reader, fields);
    closing.execute(reader);
    for (const auto& item : work) {
        if (!item.named)
            continue;
        auto& result = item.request.result;
        if (header(item.closing) != item.source ||
            item.closing.get<memory::Address>(owner_offset) != std::optional(item.request.pawn) ||
            !closing.complete(item.scene_ticket) || item.closing_scene != *item.request.scene) {
            result = {};
            continue;
        }
        if (!item.profile)
            continue;
        if (!model_reads.complete(*item.model_ticket) || !memory::user_range(item.loaded) ||
            !closing.complete(*item.closing_model_ticket) || item.closing_loaded != item.loaded) {
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
