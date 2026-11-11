#include "game/cs2/sampling/eye.hpp"
#include "game/cs2/entity/layout.hpp"
#include "game/cs2/eye/evaluation.hpp"
#include "game/cs2/eye/history.hpp"
#include "game/cs2/eye/profile.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include <algorithm>

namespace cs2::game {
namespace {
namespace p = eye_profile;
namespace schema = offsets::schema;
using memory::Address;
class Query {
  public:
    Query(memory::IReadChannel& reader, const Context& context, const Directory& directory)
        : reader_(reader), context_(context), directory_(directory), read_(reader) {}
    std::optional<model::EyeAngleBasis> run(const EntityRef& root) {
        // 选择器保留为本批实际观测值。是否影响角度值，取决于原生命中的分支。
        const auto selector = memory::read_value<std::int32_t>(reader_, context_.client.base + p::context);
        if (!selector)
            return {};
        const EntityRef* current = &root;
        std::optional<model::EyeAngleBasis> result;
        for (;;) {
            if (std::find(visited_.begin(), visited_.end(), current) != visited_.end() || !remember(*current))
                return {};
            visited_.push_back(current);
            const auto table = read_.get<Address>(current->address);
            if (!table || !memory::user_range(*table))
                return {};
            const auto getter = read_.get<Address>(*table + 0x570);
            if (!getter)
                return {};
            if (*getter == context_.client.base + p::world_rotation_getter) {
                result = world_basis(*current, *selector);
                break;
            }
            bool prediction = *getter == context_.client.base + p::prediction_getter;
            if (!prediction) {
                if (*getter != context_.client.base + p::getter || !prediction_route(current->address, prediction))
                    return {};
            }
            if (!prediction) {
                result = eye::history_basis(read_, context_, current->address, *selector);
                break;
            }
            const auto camera = read_.get<Address>(current->address + schema::C_BasePlayerPawn::m_pCameraServices);
            if (!camera)
                return {};
            if (*camera) {
                if (!memory::user_range(*camera))
                    return {};
                const auto camera_table = read_.get<Address>(*camera);
                if (!camera_table || !memory::user_range(*camera_table) ||
                    read_.get<Address>(*camera_table + 0xf0) !=
                        std::optional(context_.client.base + p::camera_view_getter))
                    return {};
                const auto view = read_.get<std::uint32_t>(*camera + schema::CPlayer_CameraServices::m_hViewEntity);
                if (!view)
                    return {};
                const EntityRef* target = nullptr;
                if (!resolve_view(*view, target))
                    return {};
                if (target) {
                    current = target;
                    continue;
                }
            }
            if (read_.get<Address>(*table + 0x578) != std::optional(context_.client.base + p::input_getter))
                return {};
            result = input_basis(*current, *selector);
            break;
        }
        if (!result || !directory_.verify_root(reader_, context_))
            return {};
        for (const auto* entity : identities_)
            if (!directory_.verify(reader_, *entity))
                return {};
        // 952870仅复制v_angle，不使用selector；非委托source2不因它自然切换而失效。
        // 历史lane、父变换及委托路径仍保留原有上下文核验，不泛化本规则。
        const bool context_independent =
            visited_.size() == 1 && result->source == model::EyeAngleSource::PredictionInput;
        if (!context_independent &&
            memory::read_value<std::int32_t>(reader_, context_.client.base + p::context) != selector)
            return {};
        if (!read_.close())
            return {};
        if (visited_.size() > 1) {
            result->resolved_source = result->source;
            result->source = model::EyeAngleSource::Delegated;
            result->source_entity = model::EntityId{current->handle};
            result->delegation_depth = static_cast<std::uint32_t>(visited_.size() - 1);
        }
        return result;
    }

  private:
    bool remember(const EntityRef& entity) {
        if (!directory_.verify(reader_, entity))
            return false;
        if (std::find(identities_.begin(), identities_.end(), &entity) == identities_.end())
            identities_.push_back(&entity);
        return true;
    }
    bool prediction_route(Address pawn, bool& prediction) {
        const auto predictable = read_.get<std::uint8_t>(pawn + schema::C_BaseEntity::m_bPredictable);
        const auto extra = read_.get<std::uint8_t>(pawn + p::prediction_flag);
        if (!predictable || !extra)
            return false;
        prediction = false;
        if (!*predictable && !*extra)
            return true;
        const auto a = read_.get<std::int32_t>(context_.client.base + p::gate_a);
        if (!a)
            return false;
        if (*a)
            return true;
        const auto b = read_.get<std::uint8_t>(context_.client.base + p::gate_b);
        if (!b)
            return false;
        if (*b) {
            const auto data = read_.get<Address>(context_.client.base + p::gate_c_data);
            if (!data || !memory::user_range(*data))
                return false;
            const auto c = read_.get<std::uint8_t>(*data + 0x58);
            if (!c)
                return false;
            if (*c)
                return true;
        }
        const auto slot = read_.get<std::int32_t>(context_.client.base + p::gate_slot);
        if (!slot)
            return false;
        prediction = *slot == 0;
        return true;
    }
    // 重放原生完整handle解析；已证明无效与读取失败必须区分。
    bool resolve_view(std::uint32_t handle, const EntityRef*& target) {
        target = nullptr;
        if (handle == UINT32_MAX || handle == UINT32_MAX - 1)
            return true;
        const auto table = read_.get<Address>(context_.client.base + p::native_entity_table);
        if (!table)
            return false;
        if (!*table)
            return true;
        if (!memory::user_range(*table, 64 * sizeof(Address)))
            return false;
        const auto index = handle & layout::entity_index_mask;
        const auto chunk = read_.get<Address>(*table + (index / layout::identities_per_chunk) * sizeof(Address));
        if (!chunk)
            return false;
        if (!*chunk)
            return true;
        if (!memory::user_range(*chunk, layout::identities_per_chunk * layout::identity_stride))
            return false;
        const auto identity = *chunk + (index % layout::identities_per_chunk) * layout::identity_stride;
        const auto actual = read_.get<std::uint32_t>(identity + layout::identity_handle);
        if (!actual)
            return false;
        if (*actual != handle)
            return true;
        const auto instance = read_.get<Address>(identity);
        if (!instance)
            return false;
        if (!*instance)
            return true;
        target = directory_.resolve(handle);
        return target && target->address == *instance && target->identity == identity;
    }
    std::optional<model::EyeAngleBasis> world_basis(const EntityRef& entity, std::int32_t selector) {
        const auto scene = read_.get<Address>(entity.address + schema::C_BaseEntity::m_pGameSceneNode);
        if (!scene || !memory::user_range(*scene) ||
            read_.get<Address>(*scene + schema::CGameSceneNode::m_pOwner) != std::optional(entity.address))
            return {};
        const auto dirty = read_.get<std::uint8_t>(*scene + p::scene_transform_dirty);
        if (!dirty || *dirty)
            return {};
        const auto value = read_.get<model::Angles>(*scene + schema::CGameSceneNode::m_angAbsRotation);
        if (!value || !model::finite(*value))
            return {};
        return model::EyeAngleBasis{*value, model::EyeAngleSource::WorldRotation, selector, {}};
    }
    std::optional<model::EyeAngleBasis> input_basis(const EntityRef& pawn, std::int32_t selector) {
        const auto scene = read_.get<Address>(pawn.address + schema::C_BaseEntity::m_pGameSceneNode);
        if (!scene || !memory::user_range(*scene) ||
            read_.get<Address>(*scene + schema::CGameSceneNode::m_pOwner) != std::optional(pawn.address))
            return {};
        const auto parent = read_.get<Address>(*scene + schema::CGameSceneNode::m_pParent);
        const auto input = read_.get<model::Angles>(pawn.address + schema::C_BasePlayerPawn::v_angle);
        if (!parent || !input || !model::finite(*input))
            return {};
        if (!*parent)
            return model::EyeAngleBasis{*input, model::EyeAngleSource::PredictionInput, selector, {}};
        const auto owner = read_.get<Address>(*parent + schema::CGameSceneNode::m_pOwner);
        if (!owner)
            return {};
        // GetMoveParent原生返回空owner时继续使用自己的输入角。
        if (!*owner)
            return model::EyeAngleBasis{*input, model::EyeAngleSource::PredictionInput, selector, {}};
        const auto* parent_entity = directory_.find_address(*owner);
        if (!parent_entity || !remember(*parent_entity))
            return {};
        const auto primary_scene = read_.get<Address>(*owner + schema::C_BaseEntity::m_pGameSceneNode);
        if (!primary_scene || !memory::user_range(*primary_scene) ||
            read_.get<Address>(*primary_scene + schema::CGameSceneNode::m_pOwner) != owner)
            return {};
        const auto dirty = read_.get<std::uint8_t>(*primary_scene + p::scene_transform_dirty);
        // 原生world变换尚未求值时不读取旧矩阵，也不主动调用游戏代码使其更新。
        if (!dirty || *dirty)
            return {};
        const auto rotation = read_.get<eye::Quaternion>(*primary_scene + schema::CGameSceneNode::m_nodeToWorld + 16);
        if (!rotation || !eye::finite(*rotation))
            return {};
        const auto value = eye::angles(eye::compose(*rotation, eye::quaternion(*input)));
        if (!model::finite(value))
            return {};
        return model::EyeAngleBasis{value, model::EyeAngleSource::ParentTransformedInput, selector, {}};
    }
    memory::IReadChannel& reader_;
    const Context& context_;
    const Directory& directory_;
    eye::Observation read_;
    std::vector<const EntityRef*> identities_, visited_;
};
} // namespace
std::optional<model::EyeAngleBasis> sample_eye_basis(memory::IReadChannel& reader, const Context& context,
                                                     const Directory& directory, const EntityRef& pawn) {
    if (context.build != eye_profile::build || pawn.kind != EntityKind::PlayerPawn)
        return {};
    return Query(reader, context, directory).run(pawn);
}
} // namespace cs2::game
