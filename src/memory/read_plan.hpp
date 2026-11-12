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
        const auto ticket = requests_.size();
        requests_.push_back({address, destination, bytes});
        return ticket;
    }
    template <class T> Ticket add(Address address, T& destination) {
        static_assert(std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>);
        return add(address, &destination, sizeof(T));
    }
    void execute(IReadChannel& reader) {
        if (executed_)
            throw std::logic_error("A read plan can only execute once");
        if (!requests_.empty())
            reader.read_many(requests_);
        executed_ = true;
    }
    [[nodiscard]] bool complete(Ticket ticket) const {
        const auto& request = requests_.at(ticket);
        return executed_ && request.result.complete(request.bytes);
    }
    [[nodiscard]] const TransferResult& result(Ticket ticket) const { return requests_.at(ticket).result; }

  private:
    std::vector<ReadRequest> requests_;
    bool executed_ = false;
};
} // namespace cs2::memory
