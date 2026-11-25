#pragma once
#include "memory/read_channel.hpp"
#include <stdexcept>
#include <vector>

namespace cs2::memory {
// 一个观察阶段的一次性读取计划。只借用调用方独占且稳定的目标缓冲；
// 不解释业务、不缓存读值，也不把后端的单次调用上限变成对象数量限制。
class ReadPlan {
  public:
    using Ticket = std::size_t;
    explicit ReadPlan(std::size_t capacity = 0) { requests_.reserve(capacity); }
    ReadPlan(const ReadPlan&) = delete;
    ReadPlan& operator=(const ReadPlan&) = delete;
    Ticket add(Address address, void* destination, std::size_t bytes) {
        if (executed_)
            throw std::logic_error("Cannot append to an executed read plan");
        const auto ticket = dependent_ ? steps_.size() : requests_.size();
        if (dependent_)
            steps_.push_back({address, destination, bytes});
        else
            requests_.push_back({address, destination, bytes});
        return ticket;
    }
    template <class T> Ticket add(Address address, T& destination) {
        static_assert(std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>);
        return add(address, &destination, sizeof(T));
    }
    Ticket pointer(Address address) { return append_pointer(address, direct_read); }
    Ticket pointer_from(Ticket parent, std::int64_t offset) {
        require_parent(parent);
        return append_pointer(static_cast<Address>(offset), parent);
    }
    Ticket add_from(Ticket parent, std::int64_t offset, void* destination, std::size_t bytes) {
        require_parent(parent);
        const auto ticket = steps_.size();
        steps_.push_back({static_cast<Address>(offset), destination, bytes, parent});
        return ticket;
    }
    template <class T> Ticket add_from(Ticket parent, std::int64_t offset, T& destination) {
        static_assert(std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>);
        return add_from(parent, offset, &destination, sizeof(T));
    }
    void execute(IReadChannel& reader) {
        if (executed_)
            throw std::logic_error("A read plan can only execute once");
        if (dependent_)
            reader.read_dependent(steps_);
        else if (!requests_.empty())
            reader.read_many(requests_);
        executed_ = true;
    }
    [[nodiscard]] bool complete(Ticket ticket) const {
        return executed_ && result(ticket).complete(dependent_ ? steps_.at(ticket).bytes : requests_.at(ticket).bytes);
    }
    [[nodiscard]] const TransferResult& result(Ticket ticket) const {
        return dependent_ ? steps_.at(ticket).result : requests_.at(ticket).result;
    }
    [[nodiscard]] std::optional<Address> pointer_value(Ticket ticket) const {
        return dependent_ && steps_.at(ticket).kind == ReadKind::Pointer && complete(ticket)
                   ? std::optional(steps_.at(ticket).pointer)
                   : std::nullopt;
    }
    [[nodiscard]] std::optional<Address> source_address(Ticket ticket) const {
        if (!executed_)
            return {};
        if (!dependent_)
            return requests_.at(ticket).address;
        const auto& step = steps_.at(ticket);
        if (step.parent == direct_read)
            return step.address;
        const auto parent = pointer_value(step.parent);
        return parent ? relative_address(*parent, step.address, step.bytes) : std::nullopt;
    }

  private:
    std::vector<ReadRequest> requests_;
    std::vector<DependentRead> steps_;
    bool dependent_ = false;
    bool executed_ = false;
    void promote() {
        if (executed_)
            throw std::logic_error("Cannot append to an executed read plan");
        if (dependent_)
            return;
        steps_.reserve(requests_.capacity());
        for (const auto& request : requests_)
            steps_.push_back({request.address, request.destination, request.bytes});
        requests_.clear();
        dependent_ = true;
    }
    void require_parent(Ticket parent) {
        if (executed_)
            throw std::logic_error("Cannot append to an executed read plan");
        if (!dependent_ || parent >= steps_.size() || steps_[parent].kind != ReadKind::Pointer)
            throw std::invalid_argument("Read dependency must refer to an earlier pointer");
    }
    Ticket append_pointer(Address address, Ticket parent) {
        promote();
        const auto ticket = steps_.size();
        steps_.push_back({address, nullptr, sizeof(Address), parent, ReadKind::Pointer});
        return ticket;
    }
};
} // namespace cs2::memory
