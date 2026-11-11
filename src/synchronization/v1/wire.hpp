#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace cs2::synchronization::v1::wire {
using Bytes = std::vector<std::byte>;
inline constexpr std::size_t body_limit = 16U * 1024U * 1024U;
class Error final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// 只实现本协议使用的protobuf wire类型；正文上限在每次扩容前检查。
class Writer {
  public:
    Bytes bytes;
    void uint32(std::uint32_t field, std::uint32_t value);
    void sint32(std::uint32_t field, std::int32_t value);
    void boolean(std::uint32_t field, bool value);
    std::size_t fixed64(std::uint32_t field, std::uint64_t value);
    void fixed32(std::uint32_t field, std::uint32_t value);
    void real(std::uint32_t field, float value);
    void real(std::uint32_t field, double value);
    std::size_t blob(std::uint32_t field, std::span<const std::byte> value);
    void text(std::uint32_t field, std::string_view value);
    void message(std::uint32_t field, const Writer& value) { (void)blob(field, value.bytes); }

  private:
    void reserve(std::size_t count);
    void varint(std::uint64_t value);
    void key(std::uint32_t field, unsigned type);
    void little(std::uint64_t value, std::size_t width);
};

struct Field {
    std::uint32_t number{};
    unsigned type{};
    std::uint64_t scalar{};
    std::span<const std::byte> bytes;
    void require(unsigned expected) const;
};
class Reader {
  public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {
        if (bytes.size() > body_limit)
            throw Error("protobuf body exceeds resource limit");
    }
    bool next(Field&);

  private:
    std::span<const std::byte> bytes_;
    std::uint64_t varint();
    std::uint64_t little(std::size_t width);
};
void patch_fixed64(std::span<std::byte> bytes, std::size_t offset, std::uint64_t value);
} // namespace cs2::synchronization::v1::wire
