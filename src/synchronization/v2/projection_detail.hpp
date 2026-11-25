#pragma once
#include "model/recoil.hpp"
#include "synchronization/v2/wire.hpp"

namespace cs2::model {
struct Bomb;
struct Local;
struct Player;
struct Projectile;
struct Weapon;
struct World;
} // namespace cs2::model

namespace cs2::synchronization::v2::projection {
using wire::Writer;
inline void scalar(Writer& w, unsigned f, bool v) {
    w.boolean(f, v);
}
inline void scalar(Writer& w, unsigned f, std::int32_t v) {
    w.sint32(f, v);
}
inline void scalar(Writer& w, unsigned f, std::uint32_t v) {
    w.uint32(f, v);
}
inline void scalar(Writer& w, unsigned f, std::uint16_t v) {
    w.uint32(f, v);
}
inline void scalar(Writer& w, unsigned f, std::uint8_t v) {
    w.uint32(f, v);
}
inline void scalar(Writer& w, unsigned f, float v) {
    w.real(f, v);
}
inline void scalar(Writer& w, unsigned f, const std::string& v) {
    w.text(f, v);
}
inline void scalar(Writer& w, unsigned f, model::EntityId v) {
    Writer child;
    child.fixed32(1, v.handle);
    w.message(f, child);
}
inline void scalar(Writer& w, unsigned f, model::Vec3 v) {
    Writer child;
    child.real(1, v.x);
    child.real(2, v.y);
    child.real(3, v.z);
    w.message(f, child);
}
inline void scalar(Writer& w, unsigned f, model::Angles v) {
    Writer child;
    child.real(1, v.pitch);
    child.real(2, v.yaw);
    child.real(3, v.roll);
    w.message(f, child);
}
inline void scalar(Writer& w, unsigned f, model::Quaternion v) {
    Writer child;
    child.real(1, v.x);
    child.real(2, v.y);
    child.real(3, v.z);
    child.real(4, v.w);
    w.message(f, child);
}
template <class T> void put(Writer& w, unsigned f, const std::optional<T>& v) {
    if (v)
        scalar(w, f, *v);
}
inline void scalar(Writer& w, unsigned f, model::GameTimePoint time) {
    Writer child;
    child.sint32(1, time.tick);
    child.real(2, time.fraction);
    w.message(f, child);
}

Writer entity(const model::EntityState&);
Writer weapon(const model::Weapon&);
Writer player(const model::Player&);
Writer bomb(const model::Bomb&);
Writer projectile(const model::Projectile&);
Writer local(const model::Local&);
Writer world(const model::World&);
} // namespace cs2::synchronization::v2::projection
