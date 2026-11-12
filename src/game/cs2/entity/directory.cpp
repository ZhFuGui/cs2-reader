#include "game/cs2/entity/directory.hpp"
#include "game/cs2/entity/identity_observation.hpp"
#include "game/cs2/entity/layout.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "memory/block_reader.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace cs2::game {
namespace {
EntityKind classify_name(std::string_view name) {
    if (name == "cs_player_controller")
        return EntityKind::Controller;
    if (name == "cs_player_pawn")
        return EntityKind::PlayerPawn;
    if (name.starts_with("weapon_"))
        return EntityKind::Weapon;
    if (name == "planted_c4")
        return EntityKind::Bomb;
    if (name == "smokegrenade_projectile" || name == "flashbang_projectile" || name == "hegrenade_projectile" ||
        name == "molotov_projectile" || name == "decoy_projectile" || name == "sensorgrenade_projectile" ||
        name == "snowball_projectile")
        return EntityKind::Projectile;
    if (name == "inferno")
        return EntityKind::Inferno;
    if (name == "hostage_entity")
        return EntityKind::Hostage;
    if (name == "chicken")
        return EntityKind::Chicken;
    return EntityKind::Unknown;
}
} // namespace
void Directory::discover(memory::IReadChannel& channel, const Context& context) {
    const auto observed =
        memory::read_value<memory::Address>(channel, context.client.base + offsets::client::dwEntityList);
    if (!observed || !memory::user_range(*observed))
        throw std::runtime_error("Entity directory is not available");
    root = *observed;
    const auto highest =
        memory::read_value<std::int32_t>(channel, root + offsets::client::dwGameEntitySystem_highestEntityIndex);
    if (!highest)
        throw std::runtime_error("Cannot read highest entity index");
    if (*highest < -1 || *highest > static_cast<std::int32_t>(layout::entity_index_mask))
        throw std::runtime_error("Entity index exceeds the handle layout; refusing to truncate the list");
    reported_highest_index = *highest;
    allocated_chunks = 0;
    constexpr auto max_chunks = (layout::entity_index_mask + 1) / layout::identities_per_chunk;
    memory::Block table;
    table.read(channel, root + layout::chunk_table, max_chunks * sizeof(memory::Address));
    std::size_t size = static_cast<std::size_t>(*highest + 1);
    for (std::size_t chunk = 0; chunk < max_chunks; ++chunk) {
        const auto pointer = table.get<memory::Address>(chunk * sizeof(memory::Address));
        if (!pointer)
            throw std::runtime_error("Cannot completely observe the entity chunk table");
        if (*pointer) {
            ++allocated_chunks;
            size = std::max(size, (chunk + 1) * layout::identities_per_chunk);
        }
    }
    // highestEntityIndex 不能作为所有客户端实体的硬上界；预测武器可能在其后。
    slots.clear();
    slots.resize(size);
    const auto chunks = (slots.size() + layout::identities_per_chunk - 1) / layout::identities_per_chunk;
    memory::Block records;
    for (std::size_t chunk = 0; chunk < chunks; ++chunk) {
        const auto start = chunk * layout::identities_per_chunk;
        const auto count = std::min(layout::identities_per_chunk, slots.size() - start);
        const auto address = table.get<memory::Address>(chunk * sizeof(memory::Address));
        if (!address || (*address && !memory::user_range(*address, count * layout::identity_stride))) {
            for (std::size_t i = 0; i < count; ++i)
                slots[start + i].state = address ? SlotState::Invalid : SlotState::Unreadable;
            continue;
        }
        if (!*address) {
            for (std::size_t i = 0; i < count; ++i)
                slots[start + i].state = SlotState::Empty;
            continue;
        }
        records.read(channel, *address, count * layout::identity_stride);
        for (std::size_t i = 0; i < count; ++i) {
            auto& slot = slots[start + i];
            const auto offset = i * layout::identity_stride;
            const auto instance = records.get<memory::Address>(offset);
            if (!instance) {
                slot.state = SlotState::Unreadable;
                continue;
            }
            if (!*instance) {
                slot.state = SlotState::Empty;
                continue;
            }
            const auto handle = records.get<std::uint32_t>(offset + layout::identity_handle);
            const auto designer =
                records.get<memory::Address>(offset + offsets::schema::CEntityIdentity::m_designerName);
            if (!handle || !designer) {
                slot.state = SlotState::Unreadable;
                continue;
            }
            if (!memory::user_range(*instance) || !memory::user_range(*designer) || *handle == UINT32_MAX ||
                (*handle & layout::entity_index_mask) != start + i) {
                slot.state = SlotState::Invalid;
                continue;
            }
            slot.entity = {*instance, *address + offset,  *designer, static_cast<std::uint32_t>(start + i), *handle,
                           {},        EntityKind::Unknown};
        }
    }
    if (!verify_root(channel, context))
        throw std::runtime_error("Entity directory changed while copying its slots");
}
void Directory::classify(memory::IReadChannel& channel, std::size_t index) {
    classify_many(channel, std::span(&index, 1));
}
void Directory::classify_many(memory::IReadChannel& channel, std::span<const std::size_t> indices) {
    std::vector<Slot*> pending;
    std::vector<const EntityRef*> identities;
    for (auto index : indices) {
        auto& slot = slots.at(index);
        if (slot.state == SlotState::Pending) {
            pending.push_back(&slot);
            identities.push_back(&slot.entity);
        }
    }
    const auto verified = verify_many(channel, identities);
    std::vector<memory::Block> names(pending.size());
    std::vector<memory::Block::Read> reads;
    for (std::size_t i = 0; i < pending.size(); ++i)
        if (verified[i])
            reads.push_back({names[i], pending[i]->entity.designer_pointer, 128});
        else
            pending[i]->state = SlotState::Changed;
    memory::Block::read_many(channel, reads);
    for (std::size_t i = 0; i < pending.size(); ++i) {
        if (!verified[i])
            continue;
        auto& slot = *pending[i];
        const auto name = names[i].text(0, 128);
        if (!name) {
            slot.state = SlotState::Unreadable;
            continue;
        }
        slot.entity.designer_name = *name;
        slot.entity.kind = classify_name(*name);
        slot.state = slot.entity.kind == EntityKind::Unknown ? SlotState::Unknown : SlotState::Classified;
    }
}
void Directory::resolve_player_aliases(memory::IReadChannel& channel) {
    namespace controller = offsets::schema::CCSPlayerController;
    struct Owner {
        const EntityRef& ref;
        memory::Fields fields;
    };
    std::vector<Owner> owners;
    for (const auto& owner : slots) {
        if (owner.entity.kind != EntityKind::Controller || owner.state != SlotState::Classified)
            continue;
        owners.push_back({owner.entity, memory::Fields(owner.entity.address,
                                                       {memory::field<std::uint32_t>(controller::m_hPlayerPawn),
                                                        memory::field<std::uint32_t>(controller::m_hObserverPawn)})});
    }
    std::vector<memory::Fields*> fields;
    for (auto& owner : owners)
        fields.push_back(&owner.fields);
    memory::Fields::read_many(channel, fields);
    struct Alias {
        Alias(const EntityRef& owner, const EntityRef& target, std::size_t offset, EntityKind kind)
            : owner(owner), target(target), offset(offset), kind(kind),
              links(target.address,
                    {memory::field<std::uint32_t>(offsets::schema::C_BasePlayerPawn::m_hController),
                     memory::field<std::uint32_t>(offsets::schema::C_BasePlayerPawn::m_hDefaultController)}),
              owner_proof(owner), target_proof(target) {}
        const EntityRef& owner;
        const EntityRef& target;
        std::size_t offset;
        EntityKind kind;
        memory::Fields links;
        IdentityObservation owner_proof, target_proof;
        std::uint32_t closing_handle{};
        memory::ReadPlan::Ticket ticket{};
    };
    std::vector<Alias> aliases;
    for (const auto& owner : owners) {
        auto add = [&](std::size_t offset, EntityKind kind, std::string_view alias) {
            const auto handle = owner.fields.get<std::uint32_t>(offset);
            const auto* target = handle ? resolve(*handle) : nullptr;
            if (target && target->kind == EntityKind::Unknown && target->designer_name == alias)
                aliases.emplace_back(owner.ref, *target, offset, kind);
        };
        add(controller::m_hPlayerPawn, EntityKind::PlayerPawn, "c_cs_player_for_precache");
        add(controller::m_hObserverPawn, EntityKind::ObserverPawn, "c_cs_observer_for_precache");
    }
    fields.clear();
    for (auto& alias : aliases)
        fields.push_back(&alias.links);
    memory::Fields::read_many(channel, fields);
    memory::ReadPlan closing;
    for (auto& alias : aliases) {
        alias.ticket = closing.add(alias.owner.address + alias.offset, alias.closing_handle);
        alias.owner_proof.schedule(closing);
        alias.target_proof.schedule(closing);
    }
    closing.execute(channel);
    for (const auto& alias : aliases) {
        // 某些练习模式复用 precache 名称。正向完整句柄、反向控制器和两端身份必须同时通过。
        const auto expected = std::optional(alias.owner.handle);
        if ((alias.links.get<std::uint32_t>(offsets::schema::C_BasePlayerPawn::m_hController) != expected &&
             alias.links.get<std::uint32_t>(offsets::schema::C_BasePlayerPawn::m_hDefaultController) != expected) ||
            !closing.complete(alias.ticket) || alias.closing_handle != alias.target.handle ||
            !alias.owner_proof.matches(closing) || !alias.target_proof.matches(closing))
            continue;
        auto& slot = slots[alias.target.index];
        if (slot.entity.kind == EntityKind::Unknown) {
            slot.entity.kind = alias.kind;
            slot.state = SlotState::Classified;
        }
    }
}
const EntityRef* Directory::resolve(std::uint32_t handle) const noexcept {
    if (handle == UINT32_MAX)
        return nullptr;
    const auto index = handle & layout::entity_index_mask;
    if (index >= slots.size())
        return nullptr;
    const auto& slot = slots[index];
    if ((slot.state != SlotState::Classified && slot.state != SlotState::Unknown) || slot.entity.handle != handle)
        return nullptr;
    return &slot.entity;
}
const EntityRef* Directory::find_address(memory::Address address) const noexcept {
    for (const auto& slot : slots)
        if ((slot.state == SlotState::Classified || slot.state == SlotState::Unknown) && slot.entity.address == address)
            return &slot.entity;
    return nullptr;
}
bool Directory::verify(memory::IReadChannel& channel, const EntityRef& ref) const {
    IdentityObservation observation(ref);
    memory::ReadPlan plan(4);
    observation.schedule(plan);
    plan.execute(channel);
    return observation.matches(plan);
}
std::vector<std::uint8_t> Directory::verify_many(memory::IReadChannel& channel,
                                                 std::span<const EntityRef* const> refs) const {
    std::vector<IdentityObservation> observations;
    observations.reserve(refs.size());
    for (const auto* ref : refs)
        observations.emplace_back(*ref);
    memory::ReadPlan plan(refs.size() * 4);
    for (auto& observation : observations)
        observation.schedule(plan);
    plan.execute(channel);
    std::vector<std::uint8_t> results;
    results.reserve(refs.size());
    for (const auto& observation : observations)
        results.push_back(observation.matches(plan));
    return results;
}
bool Directory::verify_root(memory::IReadChannel& channel, const Context& context) const {
    return memory::read_value<memory::Address>(channel, context.client.base + offsets::client::dwEntityList) ==
           std::optional(root);
}
model::Coverage Directory::coverage() const {
    model::Coverage result;
    result.slots = slots.size();
    result.reported_highest_index = reported_highest_index;
    result.allocated_chunks = allocated_chunks;
    for (const auto& slot : slots)
        switch (slot.state) {
        case SlotState::Empty:
            ++result.empty;
            break;
        case SlotState::Invalid:
            ++result.invalid;
            break;
        case SlotState::Unreadable:
            ++result.unreadable;
            break;
        case SlotState::Changed:
            ++result.changed;
            break;
        case SlotState::Unknown:
            ++result.unknown;
            break;
        case SlotState::Classified:
            ++result.classified;
            break;
        default:
            throw std::runtime_error("Entity directory has unfinished classification tasks");
        }
    return result;
}
} // namespace cs2::game
