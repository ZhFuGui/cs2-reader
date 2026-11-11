#include "game/cs2/entity/directory.hpp"
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
    auto& slot = slots[index];
    if (slot.state != SlotState::Pending)
        return;
    if (!verify(channel, slot.entity)) {
        slot.state = SlotState::Changed;
        return;
    }
    const auto name = memory::read_string(channel, slot.entity.designer_pointer, 128);
    if (!name) {
        slot.state = SlotState::Unreadable;
        return;
    }
    slot.entity.designer_name = *name;
    slot.entity.kind = classify_name(*name);
    slot.state = slot.entity.kind == EntityKind::Unknown ? SlotState::Unknown : SlotState::Classified;
}
void Directory::resolve_player_aliases(memory::IReadChannel& channel) {
    namespace controller = offsets::schema::CCSPlayerController;
    for (const auto& owner : slots) {
        if (owner.entity.kind != EntityKind::Controller || owner.state != SlotState::Classified)
            continue;
        const memory::Fields fields(channel, owner.entity.address,
                                    {memory::field<std::uint32_t>(controller::m_hPlayerPawn),
                                     memory::field<std::uint32_t>(controller::m_hObserverPawn)});
        auto resolve_alias = [&](std::size_t offset, EntityKind kind, std::string_view alias) {
            const auto handle = fields.get<std::uint32_t>(offset);
            const auto* target = handle ? resolve(*handle) : nullptr;
            if (!target || target->kind != EntityKind::Unknown || target->designer_name != alias)
                return;
            // 某些练习模式复用 precache 名称。必须同时确认正向完整句柄和反向控制器，不能按名称放行模型。
            const memory::Fields back_link(
                channel, target->address,
                {memory::field<std::uint32_t>(offsets::schema::C_BasePlayerPawn::m_hController),
                 memory::field<std::uint32_t>(offsets::schema::C_BasePlayerPawn::m_hDefaultController)});
            const auto expected = std::optional(owner.entity.handle);
            if ((back_link.get<std::uint32_t>(offsets::schema::C_BasePlayerPawn::m_hController) != expected &&
                 back_link.get<std::uint32_t>(offsets::schema::C_BasePlayerPawn::m_hDefaultController) != expected) ||
                memory::read_value<std::uint32_t>(channel, owner.entity.address + offset) != handle ||
                !verify(channel, owner.entity) || !verify(channel, *target))
                return;
            auto& slot = slots[target->index];
            slot.entity.kind = kind;
            slot.state = SlotState::Classified;
        };
        resolve_alias(controller::m_hPlayerPawn, EntityKind::PlayerPawn, "c_cs_player_for_precache");
        resolve_alias(controller::m_hObserverPawn, EntityKind::ObserverPawn, "c_cs_observer_for_precache");
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
    memory::Address identity = 0, instance = 0, designer = 0;
    std::uint32_t handle = 0;
    std::array<memory::ReadRequest, 4> requests{
        {{ref.address + offsets::schema::CEntityInstance::m_pEntity, &identity, sizeof(identity)},
         {ref.identity, &instance, sizeof(instance)},
         {ref.identity + layout::identity_handle, &handle, sizeof(handle)},
         {ref.identity + offsets::schema::CEntityIdentity::m_designerName, &designer, sizeof(designer)}}};
    channel.read_many(requests);
    return std::all_of(requests.begin(), requests.end(), [](const auto& r) { return r.result.complete(r.bytes); }) &&
           identity == ref.identity && instance == ref.address && handle == ref.handle &&
           designer == ref.designer_pointer;
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
