#include "game/cs2/entity/layout.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "game/cs2/sampling/player.hpp"

namespace cs2::game {
namespace {
using Address = memory::Address;
using Ticket = memory::ReadPlan::Ticket;
namespace controller = offsets::schema::CCSPlayerController;
namespace pawn = offsets::schema::C_BasePlayerPawn;
namespace weapon = offsets::schema::CPlayer_WeaponServices;
struct VectorHeader {
    std::int32_t count;
    std::uint32_t reserved;
    Address data;
};
struct Link {
    std::optional<Address> expected;
    Ticket pointer{};
    Address closing{};
    std::optional<Ticket> close;
};
struct Work {
    explicit Work(const PlayerSupplementRead& read) : request(read) {}
    const PlayerSupplementRead& request;
    Link name, money, items, weapons;
    memory::Block text, handles;
    std::int32_t cash{};
    std::uint8_t helmet{}, defuser{};
    std::uint32_t active{};
    VectorHeader inventory{}, closing_inventory{};
    std::optional<Ticket> cash_ticket, helmet_ticket, defuser_ticket, active_ticket, inventory_ticket, inventory_close,
        handles_pointer;
    bool inventory_valid = false;
};
bool schedule_link(memory::ReadPlan& plan, Link& link, const EntitySample& source, std::size_t offset) {
    link.expected = source.fields.get<Address>(offset);
    if (!link.expected || !memory::user_range(*link.expected)) {
        link.expected.reset();
        return false;
    }
    link.pointer = plan.pointer(source.ref.address + offset);
    return true;
}
void close_link(memory::ReadPlan& plan, Link& link, const EntitySample& source, std::size_t offset) {
    if (link.expected)
        link.close = plan.add(source.ref.address + offset, link.closing);
}
bool valid_link(const memory::ReadPlan& opening, const memory::ReadPlan& closing, const Link& link) {
    return link.expected && opening.pointer_value(link.pointer) == link.expected && link.close &&
           closing.complete(*link.close) && link.closing == *link.expected;
}
} // namespace
void sample_player_supplements(memory::IReadChannel& reader, std::span<const PlayerSupplementRead> requests) {
    std::vector<Work> work;
    work.reserve(requests.size());
    for (const auto& request : requests)
        work.emplace_back(request);
    memory::ReadPlan opening;
    std::vector<memory::Block::PlannedRead> blocks;
    for (auto& item : work) {
        auto& result = item.request.result;
        result.sanitized_name.reset();
        result.money.reset();
        if (schedule_link(opening, item.name, item.request.controller, controller::m_sSanitizedPlayerName))
            blocks.push_back({item.text, item.text.schedule_from(opening, item.name.pointer, 0, 128)});
        if (schedule_link(opening, item.money, item.request.controller, controller::m_pInGameMoneyServices))
            item.cash_ticket = opening.add_from(
                item.money.pointer, offsets::schema::CCSPlayerController_InGameMoneyServices::m_iAccount, item.cash);
        if (!item.request.body || !result.body)
            continue;
        auto& body = *result.body;
        body.helmet.reset();
        body.defuser.reset();
        body.active_weapon.reset();
        body.weapon.reset();
        body.equipped_weapons.clear();
        if (schedule_link(opening, item.items, *item.request.body, pawn::m_pItemServices)) {
            item.helmet_ticket = opening.add_from(item.items.pointer,
                                                  offsets::schema::CCSPlayer_ItemServices::m_bHasHelmet, item.helmet);
            item.defuser_ticket = opening.add_from(
                item.items.pointer, offsets::schema::CCSPlayer_ItemServices::m_bHasDefuser, item.defuser);
        }
        if (schedule_link(opening, item.weapons, *item.request.body, pawn::m_pWeaponServices)) {
            item.active_ticket = opening.add_from(item.weapons.pointer, weapon::m_hActiveWeapon, item.active);
            item.inventory_ticket = opening.add_from(item.weapons.pointer, weapon::m_hMyWeapons, item.inventory);
        }
    }
    opening.execute(reader);
    memory::Block::finish_many(reader, opening, blocks);

    // 数量必须由本轮服务头得出；第二个计划完整复制成员，不预读固定上限的列表。
    memory::ReadPlan members;
    blocks.clear();
    for (auto& item : work) {
        if (!item.inventory_ticket || !opening.complete(*item.inventory_ticket) ||
            opening.pointer_value(item.weapons.pointer) != item.weapons.expected)
            continue;
        const auto& vector = item.inventory;
        if (vector.count < 0 || static_cast<std::uint32_t>(vector.count) > layout::entity_index_mask + 1 ||
            (vector.count && !memory::user_range(vector.data, std::size_t(vector.count) * sizeof(std::uint32_t))))
            continue;
        item.inventory_valid = true;
        if (vector.count) {
            item.handles_pointer =
                members.pointer(*item.weapons.expected + weapon::m_hMyWeapons + offsetof(VectorHeader, data));
            blocks.push_back(
                {item.handles, item.handles.schedule_from(members, *item.handles_pointer, 0,
                                                          std::size_t(vector.count) * sizeof(std::uint32_t))});
        }
    }
    members.execute(reader);
    memory::Block::finish_many(reader, members, blocks);

    memory::ReadPlan closing;
    for (auto& item : work) {
        close_link(closing, item.name, item.request.controller, controller::m_sSanitizedPlayerName);
        close_link(closing, item.money, item.request.controller, controller::m_pInGameMoneyServices);
        if (item.request.body) {
            close_link(closing, item.items, *item.request.body, pawn::m_pItemServices);
            close_link(closing, item.weapons, *item.request.body, pawn::m_pWeaponServices);
        }
        if (item.inventory_valid)
            item.inventory_close = closing.add(*item.weapons.expected + weapon::m_hMyWeapons, item.closing_inventory);
    }
    closing.execute(reader);
    for (const auto& item : work) {
        auto& result = item.request.result;
        if (valid_link(opening, closing, item.name))
            result.sanitized_name = item.text.text(0, 128);
        if (valid_link(opening, closing, item.money) && opening.complete(*item.cash_ticket))
            result.money = item.cash;
        if (!result.body)
            continue;
        auto& body = *result.body;
        if (valid_link(opening, closing, item.items)) {
            if (opening.complete(*item.helmet_ticket) && item.helmet <= 1)
                body.helmet = item.helmet != 0;
            if (opening.complete(*item.defuser_ticket) && item.defuser <= 1)
                body.defuser = item.defuser != 0;
        }
        if (!valid_link(opening, closing, item.weapons))
            continue;
        if (opening.complete(*item.active_ticket) && item.active != UINT32_MAX)
            body.active_weapon = model::EntityId{item.active};
        if (!item.inventory_valid || !closing.complete(*item.inventory_close) ||
            item.closing_inventory.count != item.inventory.count ||
            item.closing_inventory.data != item.inventory.data ||
            (item.handles_pointer &&
             members.pointer_value(*item.handles_pointer) != std::optional(item.inventory.data)))
            continue;
        for (std::int32_t i = 0; i < item.inventory.count; ++i)
            if (const auto handle = item.handles.get<std::uint32_t>(std::size_t(i) * sizeof(std::uint32_t));
                handle && *handle != UINT32_MAX)
                body.equipped_weapons.push_back({*handle});
    }
}
} // namespace cs2::game
