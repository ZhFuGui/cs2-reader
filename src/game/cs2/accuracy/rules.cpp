#include "game/cs2/accuracy/rules.hpp"
#include "game/cs2/accuracy/profile.hpp"
namespace cs2::game {
namespace {
struct Description { std::size_t root; std::string_view name; bool real; };
constexpr std::array descriptions{
    Description{accuracy_profile::force,"weapon_accuracy_forcespread",true},
    Description{accuracy_profile::nospread,"weapon_accuracy_nospread",false},
    Description{accuracy_profile::air_scale,"weapon_air_spread_scale",true},
    Description{accuracy_profile::jump_impulse,"sv_jump_impulse",true},
    Description{accuracy_profile::lateral_enabled,"sv_strafing_inaccuracy_enabled",false},
    Description{accuracy_profile::lateral_bias,"sv_strafing_inaccuracy_bias",true},
    Description{accuracy_profile::lateral_scale,"sv_strafing_inaccuracy_scale",true}};
}
AccuracyRules::AccuracyRules(memory::IReadChannel& reader, const Context& context) {
    if (context.build != accuracy_profile::build)
        return;
    for (std::size_t i=0;i<descriptions.size();++i) {
        const auto& d=descriptions[i];auto& v=values_[i];
        proofs_[i]=std::make_unique<memory::Observation>(reader);
        auto& proof=*proofs_[i];
        const auto data=proof.get<memory::Address>(context.client.base+d.root+8);
        if (!data || !memory::user_range(*data))continue;
        const auto type=proof.get<std::uint16_t>(*data+0x28);
        const auto flags=proof.get<std::uint32_t>(*data+0x30);
        const auto name=proof.get<memory::Address>(*data);
        if (!type || *type!=(d.real?7:0) || !flags || (*flags&(1u<<15)) || !name)continue;
        const auto text=proof.bytes(*name,d.name.size()+1);
        if (!text || text->back()!=std::byte{} || std::memcmp(text->data(),d.name.data(),d.name.size()))continue;
        if (d.real) {
            const auto value=proof.get<float>(*data+0x58);
            if (!value)continue;
            if (!std::isfinite(*value)) {v.status=model::AccuracyStatus::InvalidInput;continue;}
            v.real=value;
        } else {
            const auto value=proof.get<std::uint8_t>(*data+0x58);
            if (!value)continue;
            if (*value>1) {v.status=model::AccuracyStatus::InvalidInput;continue;}
            v.boolean=value;
        }
        v.status=model::AccuracyStatus::Valid;
    }
}
const AccuracyRules::Value& AccuracyRules::at(std::size_t root, std::uint32_t& used) const {
    for (std::size_t i=0;i<descriptions.size();++i)
        if (root==descriptions[i].root) {used|=1u<<i;return values_[i];}
    static const Value unavailable;
    return unavailable;
}
std::uint32_t AccuracyRules::close() {
    std::uint32_t changed=0;
    for (std::size_t i=0;i<proofs_.size();++i)
        if (proofs_[i] && !proofs_[i]->close())changed|=1u<<i;
    return changed;
}
void revoke_accuracy(model::WeaponAccuracyEvaluation& value, model::AccuracyStatus status) {
    value.inaccuracy={{},status};value.branch=model::AccuracyBranch::Unspecified;
    value.movement.reset();value.airborne.reset();value.turning.reset();value.lateral.reset();
    value.penalty.reset();value.mode_raw.reset();value.accuracy_state_time_seconds.reset();
}
} // namespace cs2::game
