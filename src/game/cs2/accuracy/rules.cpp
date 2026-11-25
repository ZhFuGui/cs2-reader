#include "game/cs2/accuracy/rules.hpp"
#include "game/cs2/accuracy/profile.hpp"
namespace cs2::game {
namespace {
constexpr std::array descriptions{
    CvarRequest{accuracy_profile::force,true,"weapon_accuracy_forcespread"},
    CvarRequest{accuracy_profile::nospread,false,"weapon_accuracy_nospread"},
    CvarRequest{accuracy_profile::air_scale,true,"weapon_air_spread_scale"},
    CvarRequest{accuracy_profile::jump_impulse,true,"sv_jump_impulse"},
    CvarRequest{accuracy_profile::lateral_enabled,false,"sv_strafing_inaccuracy_enabled"},
    CvarRequest{accuracy_profile::lateral_bias,true,"sv_strafing_inaccuracy_bias"},
    CvarRequest{accuracy_profile::lateral_scale,true,"sv_strafing_inaccuracy_scale"}};
}
AccuracyRules::AccuracyRules(memory::IReadChannel& reader, const Context& context) : reader_(reader) {
    if (context.build != accuracy_profile::build)
        return;
    batch_=std::make_unique<CvarBatch>(reader, context, descriptions);
    for (std::size_t i=0;i<descriptions.size();++i) {
        const auto& d=descriptions[i];auto& v=values_[i];
        const auto& sample=batch_->at(i);
        if (!sample.readable || sample.type!=(d.real?7:0) || (sample.flags&(1u<<15)))continue;
        if (d.real) {
            const auto value=sample.real();
            if (!value) {v.status=model::AccuracyStatus::InvalidInput;continue;}
            v.real=value;
        } else {
            const auto value=sample.boolean();
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
    if (batch_) {
        const auto matches=batch_->close(reader_);
        for (std::size_t i=0;i<matches.size();++i)
            if (batch_->at(i).readable && !matches[i])changed|=1u<<i;
    }
    return changed;
}
void revoke_accuracy(model::WeaponAccuracyEvaluation& value, model::AccuracyStatus status) {
    value.inaccuracy={{},status};value.branch=model::AccuracyBranch::Unspecified;
    value.movement.reset();value.airborne.reset();value.turning.reset();value.lateral.reset();
    value.penalty.reset();value.mode_raw.reset();value.accuracy_state_time_seconds.reset();
}
} // namespace cs2::game
