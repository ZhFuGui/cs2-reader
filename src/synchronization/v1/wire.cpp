#include "synchronization/v1/wire.hpp"
#include <cmath>
#include <limits>

namespace cs2::synchronization::v1::wire {
void Writer::reserve(std::size_t count) {
    if (count > body_limit || bytes.size() > body_limit - count)
        throw Error("protobuf body exceeds resource limit");
}
void Writer::varint(std::uint64_t value) {
    std::size_t count = 1;
    for (auto remaining = value; remaining >= 0x80U; remaining >>= 7)
        ++count;
    reserve(count);
    do {
        const auto part = static_cast<unsigned char>((value & 0x7fU) | (value > 0x7fU ? 0x80U : 0));
        bytes.push_back(std::byte{part});
        value >>= 7;
    } while (value);
}
void Writer::key(std::uint32_t field, unsigned type) {
    if (!field || field > 0x1fffffffU)
        throw Error("invalid protobuf field number");
    varint((static_cast<std::uint64_t>(field) << 3) | type);
}
void Writer::little(std::uint64_t value, std::size_t width) {
    reserve(width);
    for (std::size_t i = 0; i < width; ++i)
        bytes.push_back(std::byte((value >> (8 * i)) & 0xffU));
}
void Writer::uint32(std::uint32_t field, std::uint32_t value) {
    key(field, 0);
    varint(value);
}
void Writer::sint32(std::uint32_t field, std::int32_t value) {
    const auto bits = static_cast<std::uint32_t>(value);
    uint32(field, (bits << 1) ^ (value < 0 ? std::numeric_limits<std::uint32_t>::max() : 0U));
}
void Writer::boolean(std::uint32_t field, bool value) {
    uint32(field, value ? 1U : 0U);
}
std::size_t Writer::fixed64(std::uint32_t field, std::uint64_t value) {
    key(field, 1);
    const auto offset = bytes.size();
    little(value, 8);
    return offset;
}
void Writer::fixed32(std::uint32_t field, std::uint32_t value) {
    key(field, 5);
    little(value, 4);
}
void Writer::real(std::uint32_t field, float value) {
    if (!std::isfinite(value))
        throw Error("non-finite float");
    fixed32(field, std::bit_cast<std::uint32_t>(value));
}
void Writer::real(std::uint32_t field, double value) {
    if (!std::isfinite(value))
        throw Error("non-finite double");
    (void)fixed64(field, std::bit_cast<std::uint64_t>(value));
}
std::size_t Writer::blob(std::uint32_t field, std::span<const std::byte> value) {
    key(field, 2);
    varint(value.size());
    reserve(value.size());
    const auto offset = bytes.size();
    bytes.insert(bytes.end(), value.begin(), value.end());
    return offset;
}
void Writer::text(std::uint32_t field, std::string_view value) {
    (void)blob(field, std::as_bytes(std::span{value.data(), value.size()}));
}
void Field::require(unsigned expected) const {
    if (type != expected)
        throw Error("protobuf wire type mismatch");
}
std::uint64_t Reader::varint() {
    std::uint64_t result = 0;
    for (unsigned i = 0; i < 10; ++i) {
        if (bytes_.empty())
            throw Error("truncated protobuf varint");
        const auto value = std::to_integer<unsigned>(bytes_.front());
        bytes_ = bytes_.subspan(1);
        if (i == 9 && value > 1)
            throw Error("protobuf varint overflow");
        result |= static_cast<std::uint64_t>(value & 0x7fU) << (7U * i);
        if (!(value & 0x80U))
            return result;
    }
    throw Error("protobuf varint overflow");
}
std::uint64_t Reader::little(std::size_t width) {
    if (bytes_.size() < width)
        throw Error("truncated protobuf fixed field");
    std::uint64_t result = 0;
    for (std::size_t i = 0; i < width; ++i)
        result |= std::to_integer<std::uint64_t>(bytes_[i]) << (i * 8);
    bytes_ = bytes_.subspan(width);
    return result;
}
bool Reader::next(Field& field) {
    if (bytes_.empty())
        return false;
    const auto tag = varint();
    if ((tag >> 3) == 0 || (tag >> 3) > 0x1fffffffU)
        throw Error("invalid protobuf tag");
    field = {};
    field.number = static_cast<std::uint32_t>(tag >> 3);
    field.type = static_cast<unsigned>(tag & 7U);
    switch (field.type) {
    case 0:
        field.scalar = varint();
        break;
    case 1:
        field.scalar = little(8);
        break;
    case 5:
        field.scalar = little(4);
        break;
    case 2: {
        const auto length = varint();
        if (length > bytes_.size())
            throw Error("truncated protobuf byte field");
        field.bytes = bytes_.first(static_cast<std::size_t>(length));
        bytes_ = bytes_.subspan(static_cast<std::size_t>(length));
        break;
    }
    default:
        throw Error("protobuf groups and invalid wire types are unsupported");
    }
    return true;
}
void patch_fixed64(std::span<std::byte> bytes, std::size_t offset, std::uint64_t value) {
    if (offset > bytes.size() || bytes.size() - offset < 8)
        throw Error("invalid stamp offset");
    for (std::size_t i = 0; i < 8; ++i)
        bytes[offset + i] = std::byte((value >> (8 * i)) & 0xffU);
}
} // namespace cs2::synchronization::v1::wire
