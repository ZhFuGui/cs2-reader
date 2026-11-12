#pragma once
#include "memory/read_channel.hpp"
#include <algorithm>
#include <span>
#include <string>
#include <vector>

namespace cs2::memory {
struct Field {
    std::size_t offset, size;
};
template <class T> constexpr Field field(std::size_t offset) noexcept {
    return {offset, sizeof(T)};
}
// 每个成功区间都明确标记；未读字节即使缓冲中是零，也不能被解释成字段值。
class Block {
  public:
    struct Read {
        Block& block;
        Address address;
        std::size_t bytes;
    };
    void read(IReadChannel& channel, Address address, std::size_t bytes);
    // 各请求必须引用不同的 Block；调用期间保持实例及请求数组稳定。
    static void read_many(IReadChannel&, std::span<const Read>);
    [[nodiscard]] bool valid(std::size_t offset, std::size_t bytes) const noexcept;
    template <class T> std::optional<T> get(std::size_t offset) const noexcept {
        static_assert(std::is_trivially_copyable_v<T>);
        if (!valid(offset, sizeof(T)))
            return std::nullopt;
        T value;
        std::memcpy(&value, data_.data() + offset, sizeof(T));
        return value;
    }
    std::optional<std::string> text(std::size_t offset, std::size_t capacity) const;

  private:
    std::vector<std::byte> data_;
    std::vector<unsigned char> valid_;
};
// 同对象、同次观察的字段共享连续缓冲；每个字段独立记录有效性。
// 组合读取失败时仅回退实际请求的字段，不把填充字节解释成数据。
class Fields {
  public:
    // 仅准备字段布局和独立缓冲；多个对象可随后一起 read_many。
    Fields(Address object, std::span<const Field> fields, std::span<const Field> additional = {});
    Fields(Address object, std::initializer_list<Field> fields)
        : Fields(object, std::span(fields.begin(), fields.size())) {}
    Fields(Address object, std::span<const Field> fields, std::initializer_list<Field> additional)
        : Fields(object, fields, std::span(additional.begin(), additional.size())) {}
    Fields(IReadChannel& channel, Address object, std::span<const Field> fields,
           std::span<const Field> additional = {});
    Fields(IReadChannel& channel, Address object, std::initializer_list<Field> fields)
        : Fields(channel, object, std::span(fields.begin(), fields.size())) {}
    Fields(IReadChannel& channel, Address object, std::span<const Field> fields,
           std::initializer_list<Field> additional)
        : Fields(channel, object, fields, std::span(additional.begin(), additional.size())) {}
    template <class T> std::optional<T> get(std::size_t offset) const noexcept {
        for (const auto& item : values_) {
            if (item.offset == offset && item.size >= sizeof(T) && item.valid) {
                T value;
                std::memcpy(&value, data_.data() + item.storage, sizeof(T));
                return value;
            }
        }
        return std::nullopt;
    }
    std::optional<std::string> text(std::size_t offset) const;
    // 各指针非空且互不重复；失败字段不会沿用本实例上一次读取的有效性。
    static void read_many(IReadChannel&, std::span<Fields* const>);

  private:
    struct Value {
        std::size_t offset, size, storage = 0;
        bool valid = false;
    };
    struct Cluster {
        std::size_t first, last, begin, offset, size;
    };
    Address object_;
    std::vector<Value> values_;
    std::vector<Cluster> clusters_;
    std::vector<std::byte> data_;
};
std::optional<std::string> read_string(IReadChannel&, Address, std::size_t capacity);
} // namespace cs2::memory
