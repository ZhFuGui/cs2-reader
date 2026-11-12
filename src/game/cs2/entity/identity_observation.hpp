#pragma once
#include "game/cs2/entity/directory.hpp"
#include "game/cs2/entity/layout.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "memory/read_plan.hpp"
#include <algorithm>
#include <array>

namespace cs2::game {
// 一个观察边界的身份读取。schedule 后保持自身位置稳定；开读和关闭必须各建一份。
class IdentityObservation {
  public:
    explicit IdentityObservation(const EntityRef& expected) : expected_(&expected) {}
    void schedule(memory::ReadPlan& plan) {
        tickets_ = {plan.add(expected_->address + offsets::schema::CEntityInstance::m_pEntity, identity_),
                    plan.add(expected_->identity, instance_),
                    plan.add(expected_->identity + layout::identity_handle, handle_),
                    plan.add(expected_->identity + offsets::schema::CEntityIdentity::m_designerName, designer_)};
        scheduled_ = true;
    }
    [[nodiscard]] bool matches(const memory::ReadPlan& plan) const {
        return scheduled_ &&
               std::all_of(tickets_.begin(), tickets_.end(), [&](auto ticket) { return plan.complete(ticket); }) &&
               identity_ == expected_->identity && instance_ == expected_->address && handle_ == expected_->handle &&
               designer_ == expected_->designer_pointer;
    }

  private:
    const EntityRef* expected_;
    memory::Address identity_{}, instance_{}, designer_{};
    std::uint32_t handle_{};
    std::array<memory::ReadPlan::Ticket, 4> tickets_{};
    bool scheduled_ = false;
};
} // namespace cs2::game
