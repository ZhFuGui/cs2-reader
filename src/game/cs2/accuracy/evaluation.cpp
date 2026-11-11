#include "game/cs2/accuracy/evaluation.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace cs2::accuracy {
namespace {
InaccuracyResult fail(Error error,std::string_view field) {
    InaccuracyResult result;result.error=error;result.dependency=field;return result;
}
bool finite(Velocity v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
float select(const std::array<float,2>& values,std::int32_t mode) {
    return values[mode>=0&&mode<2?static_cast<std::size_t>(mode):0];
}
float remap_clamped(float value,float low,float high) {
    if(low==high)return value-high>=0.f?1.f:0.f;
    return std::min(1.f,std::max(0.f,(value-low)/(high-low)));
}
float remap_unclamped(float value,float low,float high,float begin,float end) {
    if(low==high)return value-high>=0.f?end:begin;
    return ((value-low)*(end-begin))/(high-low)+begin;
}
float native_bias(float value,float bias) {
    const float x=std::max(0.f,std::min(1.f,value));
    const float b=std::max(std::numeric_limits<float>::min(),std::min(1.f,bias));
    const float coefficient=1.f/b-2.f;
    return x/(coefficient*(1.f-x)+1.f);
}
} // namespace

InaccuracyResult evaluate_inaccuracy(const Inputs& in) noexcept {
    if(in.ownership==Ownership::Unknown)return fail(Error::MissingInput,"ownership");
    InaccuracyResult result;
    if(in.ownership==Ownership::Absent) {
        result.value=0.f;result.branch=Branch::NoOwner;return result;
    }
    if(!in.rules.force)return fail(Error::MissingInput,"weapon_accuracy_forcespread");
    if(!std::isfinite(*in.rules.force))return fail(Error::InvalidInput,"weapon_accuracy_forcespread");
    if(*in.rules.force>0.f) {
        result.value=std::min(*in.rules.force,1.f);result.branch=Branch::Forced;return result;
    }
    if(!in.rules.no_spread)return fail(Error::MissingInput,"weapon_accuracy_nospread");
    if(*in.rules.no_spread) {result.value=0.f;result.branch=Branch::NoSpread;return result;}
    if(!in.weapon.mode)return fail(Error::MissingInput,"m_weaponMode");
    if(!in.weapon.accuracy_penalty)return fail(Error::MissingInput,"m_fAccuracyPenalty");
    if(!in.weapon.turning_inaccuracy)return fail(Error::MissingInput,"m_flTurningInaccuracy");
    if(!in.owner.velocity)return fail(Error::MissingInput,"verified_GetAbsVelocity");
    if(!in.owner.walking)return fail(Error::MissingInput,"m_bIsWalking");
    if(!in.owner.ground_entity_present)return fail(Error::MissingInput,"resolved_ground_entity");
    if(!in.data.movement)return fail(Error::MissingInput,"m_flInaccuracyMove");
    float max_speed=0.f;
    if(in.weapon.maximum_speed_path==MaximumSpeedPath::Unknown)
        return fail(Error::UnsupportedPath,"maximum_speed_getter");
    if(in.weapon.maximum_speed_path==MaximumSpeedPath::PlantingC4&&!in.weapon.started_arming)
        return fail(Error::MissingInput,"m_bStartedArming");
    if(in.weapon.maximum_speed_path==MaximumSpeedPath::PlantingC4&&*in.weapon.started_arming)max_speed=1.f;
    else {
        if(!in.data.maximum_speed)return fail(Error::MissingInput,"m_flMaxSpeed");
        max_speed=select(*in.data.maximum_speed,*in.weapon.mode);
    }
    const float move_data=select(*in.data.movement,*in.weapon.mode);
    const auto velocity=*in.owner.velocity;
    if(!finite(velocity)||!std::isfinite(max_speed)||!std::isfinite(move_data)||
       !std::isfinite(*in.weapon.accuracy_penalty)||!std::isfinite(*in.weapon.turning_inaccuracy))
        return fail(Error::InvalidInput,"normal_inputs");
    const float horizontal=std::sqrt(velocity.x*velocity.x+velocity.y*velocity.y);
    const float ratio=remap_clamped(horizontal,max_speed*.34f,max_speed*.95f);
    float movement=0.f;
    if(ratio>0.f)movement=(*in.owner.walking?ratio:std::pow(ratio,.25f))*move_data;
    result.movement=movement;
    float sum=*in.weapon.accuracy_penalty+movement;
    float airborne=0.f;
    if(!*in.owner.ground_entity_present) {
        if(!in.rules.air_scale)return fail(Error::MissingInput,"weapon_air_spread_scale");
        if(!in.rules.jump_impulse)return fail(Error::MissingInput,"sv_jump_impulse");
        if(!in.data.jump_initial)return fail(Error::MissingInput,"m_flInaccuracyJumpInitial");
        if(!in.data.jump_apex)return fail(Error::MissingInput,"m_flInaccuracyJumpApex");
        if(!std::isfinite(*in.rules.air_scale)||!std::isfinite(*in.rules.jump_impulse)||
           !std::isfinite(*in.data.jump_initial)||!std::isfinite(*in.data.jump_apex))
            return fail(Error::InvalidInput,"air_inputs");
        const float initial=(*in.rules.air_scale)*(*in.data.jump_initial);
        const float apex=(*in.rules.air_scale)*(*in.data.jump_apex);
        const float impulse=std::sqrt(*in.rules.jump_impulse);
        const float vertical=std::sqrt(std::abs(velocity.z));
        const float value=remap_unclamped(vertical,impulse*.25f,impulse,apex,initial);
        if(!std::isfinite(impulse)||!std::isfinite(vertical)||!std::isfinite(value))
            return fail(Error::NonFiniteResult,"air_arithmetic");
        airborne=value<0.f?0.f:std::min(initial+initial,value);
    }
    result.airborne=airborne;sum=sum+airborne;
    if(!in.rules.lateral_enabled)return fail(Error::MissingInput,"sv_strafing_inaccuracy_enabled");
    float lateral=0.f;
    if(*in.rules.lateral_enabled) {
        const float guard=(velocity.y*velocity.y+velocity.x*velocity.x)+velocity.z*velocity.z;
        if(guard>=.001f) {
            if(!in.owner.native_eye_angles)return fail(Error::MissingInput,"native_EyeAngles");
            if(!in.rules.lateral_bias)return fail(Error::MissingInput,"sv_strafing_inaccuracy_bias");
            if(!in.rules.lateral_scale)return fail(Error::MissingInput,"sv_strafing_inaccuracy_scale");
            const auto angle=*in.owner.native_eye_angles;
            if(!std::isfinite(angle.pitch)||!std::isfinite(angle.yaw)||
               !std::isfinite(*in.rules.lateral_bias)||!std::isfinite(*in.rules.lateral_scale))
                return fail(Error::InvalidInput,"lateral_inputs");
            const float length=std::sqrt((velocity.z*velocity.z+velocity.y*velocity.y)+velocity.x*velocity.x);
            // 原生在极端向量长度下另有归一化回退。本版本不猜该未覆盖数值域。
            if(!std::isfinite(length)||length<1e-17f||length>1e17f)
                return fail(Error::UnsupportedPath,"extreme_velocity_normalization");
            const float inverse=1.f/length;
            const Velocity unit{inverse*velocity.x,inverse*velocity.y,inverse*velocity.z};
            constexpr float radians=.01745329238474369f;
            const float yaw=angle.yaw*radians,pitch=angle.pitch*radians;
            const Velocity forward{std::cos(yaw)*std::cos(pitch),std::sin(yaw)*std::cos(pitch),-std::sin(pitch)};
            const float dot=(forward.z*unit.z+forward.y*unit.y)+forward.x*unit.x;
            const float biased=native_bias(1.f-std::abs(dot),*in.rules.lateral_bias);
            lateral=((length/250.f)*biased)*(*in.rules.lateral_scale);
        }
    }
    sum=sum+*in.weapon.turning_inaccuracy;
    sum=lateral+sum;
    const float value=std::min(1.f,sum);
    if(!std::isfinite(value)||!std::isfinite(movement)||!std::isfinite(airborne)||!std::isfinite(lateral))
        return fail(Error::NonFiniteResult,"native_arithmetic");
    result.value=value;result.turning=*in.weapon.turning_inaccuracy;result.lateral=lateral;result.branch=Branch::Normal;
    return result;
}

ScalarResult evaluate_spread(const WeaponState& weapon,const WeaponData& data) noexcept {
    if(!weapon.mode)return {{},Error::MissingInput,"m_weaponMode"};
    if(!data.spread)return {{},Error::MissingInput,"m_flSpread"};
    const float value=select(*data.spread,*weapon.mode);
    if(!std::isfinite(value))return {{},Error::InvalidInput,"m_flSpread"};
    return {value,Error::None,{}};
}
} // namespace cs2::accuracy
