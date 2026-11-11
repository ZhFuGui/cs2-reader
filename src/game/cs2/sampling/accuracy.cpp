#include "game/cs2/sampling/accuracy.hpp"
#include "game/cs2/accuracy/evaluation.hpp"
#include "game/cs2/accuracy/profile.hpp"
#include "game/cs2/accuracy/rules.hpp"
#include "game/cs2/entity/layout.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "game/cs2/sampling/eye.hpp"
#include "memory/observation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace cs2::game {
namespace {
namespace p = accuracy_profile;
namespace s = offsets::schema;
using memory::Address;
using Status = model::AccuracyStatus;
struct Failure { Status status; };
// 每个标量有自己的读取账本。Spread不继承速度、owner、规则或眼角的失败。
class Query {
  public:
    Query(memory::IReadChannel& reader, const Context& context, const Directory& directory, const EntityRef& weapon)
        : observation(reader), reader_(reader), context_(context), directory_(directory), weapon_(weapon) {}
    template <class T> T get(Address address, Status failure = Status::ReadFailed) {
        const auto value = observation.get<T>(address);
        if (!value)
            throw Failure{failure};
        return *value;
    }
    void entry(const p::Entry& expected) {
        if (get<std::array<unsigned char,16>>(context_.client.base + expected.rva) != expected.bytes)
            throw Failure{Status::UnsupportedDispatch};
    }
    Address table(Address object) {
        const auto value = get<Address>(object);
        if (!memory::user_range(value))
            throw Failure{Status::UnsupportedDispatch};
        return value;
    }
    void method(Address vtable, std::size_t slot, const p::Entry& expected) {
        if (get<Address>(vtable + slot) != context_.client.base + expected.rva)
            throw Failure{Status::UnsupportedDispatch};
        entry(expected);
    }
    Address data() {
        const auto value = get<Address>(weapon_.address + p::vdata);
        if (!memory::user_range(value))
            throw Failure{Status::InvalidInput};
        return value;
    }
    // 复现原生无效handle、缺表/块、serial不匹配和空instance的“确认不存在”。
    // 未读到这些结构时抛出相应错误；目录漏收对象不能伪装成原生解析为空。
    Address resolve(std::uint32_t handle, Status failure) {
        if (handle == UINT32_MAX)
            return 0;
        const auto root = get<Address>(context_.client.base + p::entity_table, failure);
        if (!root || handle == UINT32_MAX - 1)
            return 0;
        if (!memory::user_range(root, 64 * sizeof(Address)))
            throw Failure{failure};
        const auto index = handle & layout::entity_index_mask;
        const auto chunk = get<Address>(root + (index / layout::identities_per_chunk) * sizeof(Address), failure);
        if (!chunk)
            return 0;
        if (!memory::user_range(chunk, layout::identities_per_chunk * layout::identity_stride))
            throw Failure{failure};
        const auto identity = chunk + (index % layout::identities_per_chunk) * layout::identity_stride;
        if (get<std::uint32_t>(identity + layout::identity_handle, failure) != handle)
            return 0;
        const auto instance = get<Address>(identity, failure);
        if (instance && !memory::user_range(instance))
            throw Failure{failure};
        return instance;
    }
    template <class T> T rule(std::size_t root, std::string_view name) {
        if (rules) {
            const auto& value=rules->at(root,used_rules);
            if (value.status!=Status::Valid)throw Failure{value.status};
            if constexpr (std::is_same_v<T,float>) {
                if (!value.real)throw Failure{Status::RuleUnavailable};
                return *value.real;
            } else {
                if (!value.boolean)throw Failure{Status::RuleUnavailable};
                return *value.boolean;
            }
        }
        const auto data = get<Address>(context_.client.base + root + 8, Status::RuleUnavailable);
        if (!memory::user_range(data))
            throw Failure{Status::RuleUnavailable};
        const auto type = get<std::uint16_t>(data + 0x28, Status::RuleUnavailable);
        const auto flags = get<std::uint32_t>(data + 0x30, Status::RuleUnavailable);
        constexpr auto expected = std::is_same_v<T,float> ? 7 : 0;
        // 分槽cvar需要原生槽位选择器；尚未适配时明确拒绝，不能偷用第0槽。
        if (type != expected || (flags & (1u << 15)))
            throw Failure{Status::RuleUnavailable};
        const auto pointer = get<Address>(data, Status::RuleUnavailable);
        const auto text = observation.bytes(pointer, name.size() + 1);
        if (!text || text->back() != std::byte{} ||
            std::memcmp(text->data(), name.data(), name.size()) != 0)
            throw Failure{Status::RuleUnavailable};
        const auto value = get<T>(data + 0x58, Status::RuleUnavailable);
        if constexpr (std::is_same_v<T,float>) {
            if (!std::isfinite(value))
                throw Failure{Status::InvalidInput};
        } else if (value > 1)
            throw Failure{Status::InvalidInput};
        return value;
    }
    bool close() {
        return observation.close() && directory_.verify(reader_, weapon_) &&
               (!owner || directory_.verify(reader_, *owner)) && directory_.verify_root(reader_, context_);
    }
    memory::Observation observation;
    const EntityRef* owner = nullptr;
    const AccuracyRules* rules = nullptr;
    std::uint32_t used_rules = 0;
  private:
    memory::IReadChannel& reader_;
    const Context& context_;
    const Directory& directory_;
    const EntityRef& weapon_;
};
Status translate(accuracy::Error error) {
    switch (error) {
    case accuracy::Error::None: return Status::Valid;
    case accuracy::Error::MissingInput: return Status::ReadFailed;
    case accuracy::Error::InvalidInput: return Status::InvalidInput;
    case accuracy::Error::UnsupportedPath: return Status::UnsupportedNormalization;
    case accuracy::Error::NonFiniteResult: return Status::NonfiniteResult;
    }
    return Status::InvalidInput;
}
accuracy::InaccuracyResult inaccuracy(Query& q, memory::IReadChannel& reader, const Context& context,
                                     const Directory& directory, const EntityRef& weapon,
                                     model::WeaponAccuracyEvaluation& metadata) {
    accuracy::Inputs in;
    const auto table = q.table(weapon.address);
    q.method(table, 0xcf8, p::inaccuracy);
    q.entry(p::owner);
    const auto handle = q.get<std::uint32_t>(weapon.address + s::C_BaseEntity::m_hOwnerEntity);
    const auto owner = q.resolve(handle, Status::OwnerUnavailable);
    in.ownership = owner ? accuracy::Ownership::Present : accuracy::Ownership::Absent;
    if (!owner)
        return accuracy::evaluate_inaccuracy(in);
    q.owner = directory.resolve(handle);
    if (!q.owner || q.owner->address != owner || q.owner->kind != EntityKind::PlayerPawn ||
        !directory.verify(reader, *q.owner))
        throw Failure{Status::OwnerUnavailable};
    metadata.owner = model::EntityId{handle};
    in.rules.force = q.rule<float>(p::force, "weapon_accuracy_forcespread");
    if (*in.rules.force > 0)
        return accuracy::evaluate_inaccuracy(in);
    in.rules.no_spread = q.rule<std::uint8_t>(p::nospread, "weapon_accuracy_nospread") != 0;
    if (*in.rules.no_spread)
        return accuracy::evaluate_inaccuracy(in);
    const auto data = q.data();
    in.weapon.mode = q.get<std::int32_t>(weapon.address + s::C_CSWeaponBase::m_weaponMode);
    in.weapon.accuracy_penalty = q.get<float>(weapon.address + s::C_CSWeaponBase::m_fAccuracyPenalty);
    in.weapon.turning_inaccuracy = q.get<float>(weapon.address + s::C_CSWeaponBase::m_flTurningInaccuracy);
    const auto time = q.observation.get<float>(weapon.address + s::C_CSWeaponBase::m_flLastAccuracyUpdateTime);
    if (time && std::isfinite(*time))
        metadata.accuracy_state_time_seconds = *time;
    const auto speed_getter = q.get<Address>(table + 0xba0);
    if (speed_getter == context.client.base + p::maximum_speed.rva) {
        q.entry(p::maximum_speed);
        in.weapon.maximum_speed_path = accuracy::MaximumSpeedPath::WeaponData;
    } else if (speed_getter == context.client.base + p::c4_speed.rva) {
        q.entry(p::c4_speed);
        in.weapon.maximum_speed_path = accuracy::MaximumSpeedPath::PlantingC4;
        const auto arming = q.get<std::uint8_t>(weapon.address + s::C_C4::m_bStartedArming);
        if (arming > 1)
            throw Failure{Status::InvalidInput};
        in.weapon.started_arming = arming != 0;
    } else
        throw Failure{Status::UnsupportedDispatch};
    if (!in.weapon.started_arming.value_or(false))
        in.data.maximum_speed = q.get<std::array<float,2>>(data + s::CCSWeaponBaseVData::m_flMaxSpeed);
    q.method(table, 0xb70, p::movement);
    in.data.movement = q.get<std::array<float,2>>(data + s::CCSWeaponBaseVData::m_flInaccuracyMove);
    q.entry(p::velocity);
    q.entry(p::velocity_recompute);
    if (q.get<std::uint8_t>(owner + s::C_BaseEntity::m_nActualMoveType) == 5)
        throw Failure{Status::UnsupportedPhysics};
    if (q.get<std::uint32_t>(owner + s::C_BaseEntity::m_iEFlags) & 0x1000)
        throw Failure{Status::DirtyVelocity};
    in.owner.velocity = q.get<accuracy::Velocity>(owner + s::C_BaseEntity::m_vecAbsVelocity);
    const auto walking = q.get<std::uint8_t>(owner + s::C_CSPlayerPawn::m_bIsWalking);
    if (walking > 1)
        throw Failure{Status::InvalidInput};
    in.owner.walking = walking != 0;
    q.method(q.table(owner), 0x5c8, p::ground);
    const auto ground = q.get<std::uint32_t>(owner + s::C_BaseEntity::m_hGroundEntity, Status::GroundUnavailable);
    in.owner.ground_entity_present = q.resolve(ground, Status::GroundUnavailable) != 0;
    if (!*in.owner.ground_entity_present) {
        in.rules.air_scale = q.rule<float>(p::air_scale, "weapon_air_spread_scale");
        in.rules.jump_impulse = q.rule<float>(p::jump_impulse, "sv_jump_impulse");
        in.data.jump_initial = q.get<float>(data + s::CCSWeaponBaseVData::m_flInaccuracyJumpInitial);
        in.data.jump_apex = q.get<float>(data + s::CCSWeaponBaseVData::m_flInaccuracyJumpApex);
    }
    in.rules.lateral_enabled = q.rule<std::uint8_t>(p::lateral_enabled, "sv_strafing_inaccuracy_enabled") != 0;
    const auto v = *in.owner.velocity;
    if (*in.rules.lateral_enabled && (v.y*v.y + v.x*v.x) + v.z*v.z >= .001f) {
        const auto eye = sample_eye_basis(reader, context, directory, *q.owner);
        if (!eye)
            throw Failure{Status::EyeUnavailable};
        in.owner.native_eye_angles = accuracy::EyeAngles{eye->value.pitch, eye->value.yaw};
        in.rules.lateral_bias = q.rule<float>(p::lateral_bias, "sv_strafing_inaccuracy_bias");
        in.rules.lateral_scale = q.rule<float>(p::lateral_scale, "sv_strafing_inaccuracy_scale");
    }
    metadata.penalty = in.weapon.accuracy_penalty;
    metadata.mode_raw = in.weapon.mode;
    return accuracy::evaluate_inaccuracy(in);
}
} // namespace

model::WeaponAccuracyEvaluation sample_weapon_accuracy(memory::IReadChannel& reader, const Context& context,
                                                       const Directory& directory, const EntityRef& weapon,
                                                       const capture::IClock& clock, const AccuracyRules* rules) {
    model::WeaponAccuracyEvaluation result;
    result.model_id = p::model_id;
    result.started_qpc = clock.now();
    if (context.build != p::build || context.client.timestamp != p::timestamp || context.client.size != p::image_size) {
        result.inaccuracy.status = result.spread.status = Status::UnsupportedBuild;
    } else {
        Query spread(reader, context, directory, weapon);
        try {
            spread.method(spread.table(weapon.address), 0xbd0, p::spread);
            accuracy::WeaponState state;
            accuracy::WeaponData data;
            state.mode = spread.get<std::int32_t>(weapon.address + s::C_CSWeaponBase::m_weaponMode);
            data.spread = spread.get<std::array<float,2>>(spread.data() + s::CCSWeaponBaseVData::m_flSpread);
            const auto value = accuracy::evaluate_spread(state, data);
            result.spread = {value.value, translate(value.error)};
        } catch (const Failure& error) {
            result.spread.status = error.status;
        }
        if (!spread.close())
            result.spread = {{}, Status::SourceChanged};
        Query query(reader, context, directory, weapon);
        query.rules=rules;
        try {
            const auto value = inaccuracy(query, reader, context, directory, weapon, result);
            result.inaccuracy = {value.value, translate(value.error)};
            if (value.value) {
                result.branch = static_cast<model::AccuracyBranch>(value.branch);
                result.movement = value.movement;
                result.airborne = value.airborne;
                result.turning = value.turning;
                result.lateral = value.lateral;
            }
        } catch (const Failure& error) {
            result.inaccuracy.status = error.status;
        }
        if (!query.close()) {
            result.inaccuracy = {{}, Status::SourceChanged};
            result.owner.reset();
        }
        result.used_rules=query.used_rules;
        if (result.inaccuracy.status != Status::Valid) {
            revoke_accuracy(result,result.inaccuracy.status);
        }
    }
    result.read_finished_qpc = clock.now();
    return result;
}
} // namespace cs2::game
