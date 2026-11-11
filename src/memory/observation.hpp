#pragma once
#include "memory/read_channel.hpp"
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>
namespace cs2::memory {
// 仅保存本次查询的来源证明。没有跨帧缓存；关闭时任何使用的字节改变则撤销整组。
class Observation {
  public:
    explicit Observation(memory::IReadChannel& reader) : reader_(reader) {}
    std::optional<std::vector<std::byte>> bytes(memory::Address address, std::size_t count) {
        std::vector<std::byte> value(count);
        if (!memory::user_range(address, count) || !reader_.read(address, value.data(), count).complete(count))
            return {};
        records_.push_back({address, value});
        return value;
    }
    template <class T> std::optional<T> get(memory::Address address) {
        static_assert(std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>);
        const auto value = bytes(address, sizeof(T));
        if (!value)
            return {};
        T result;
        std::memcpy(&result, value->data(), sizeof(T));
        return result;
    }
    bool close() {
        std::size_t total = 0;
        for (const auto& record : records_) {
            if (record.value.size() > std::numeric_limits<std::size_t>::max() - total)
                throw std::length_error("Observation close buffer overflow");
            total += record.value.size();
        }
        std::vector<std::byte> scratch(total);
        std::vector<ReadRequest> requests;
        requests.reserve(records_.size());
        std::size_t offset = 0;
        for (const auto& record : records_) {
            requests.push_back(
                {record.address, record.value.empty() ? nullptr : scratch.data() + offset, record.value.size()});
            offset += record.value.size();
        }
        reader_.read_many(requests); // 关闭始终重新读取，绝不使用首次观察的缓存。
        for (std::size_t i = 0; i < records_.size(); ++i) {
            const auto& record = records_[i];
            if (!requests[i].result.complete(record.value.size()) ||
                (record.value.size() && std::memcmp(requests[i].destination, record.value.data(), record.value.size())))
                return false;
        }
        return true;
    }

  private:
    struct Record {
        memory::Address address;
        std::vector<std::byte> value;
    };
    memory::IReadChannel& reader_;
    std::vector<Record> records_;
};
} // namespace cs2::memory
