#pragma once
#include "game/cs2/session/context.hpp"
#include "model/snapshot.hpp"
#include <optional>
#include <string>
#include <vector>
namespace cs2::game {
enum class EntityKind {
    Unknown,
    Controller,
    PlayerPawn,
    ObserverPawn,
    Weapon,
    Bomb,
    Projectile,
    Inferno,
    Hostage,
    Chicken
};
enum class SlotState { Empty, Invalid, Unreadable, Changed, Unknown, Classified, Pending };
struct EntityRef {
    memory::Address address = 0, identity = 0, designer_pointer = 0;
    std::uint32_t index = 0, handle = 0;
    std::string designer_name;
    EntityKind kind = EntityKind::Unknown;
};
struct Slot {
    SlotState state = SlotState::Pending;
    EntityRef entity;
};
class Directory {
  public:
    memory::Address root = 0;
    std::int32_t reported_highest_index = -1;
    std::uint32_t allocated_chunks = 0;
    std::vector<Slot> slots;
    void discover(memory::IReadChannel&, const Context&);
    void classify(memory::IReadChannel&, std::size_t index);
    void resolve_player_aliases(memory::IReadChannel&);
    [[nodiscard]] const EntityRef* resolve(std::uint32_t handle) const noexcept;
    [[nodiscard]] const EntityRef* find_address(memory::Address) const noexcept;
    [[nodiscard]] bool verify(memory::IReadChannel&, const EntityRef&) const;
    [[nodiscard]] bool verify_root(memory::IReadChannel&, const Context&) const;
    [[nodiscard]] model::Coverage coverage() const;
};
} // namespace cs2::game
