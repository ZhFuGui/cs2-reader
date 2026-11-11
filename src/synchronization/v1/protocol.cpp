#include "synchronization/v1/protocol.hpp"
#include "cs2_reader/v1/record.hpp"
#include "cs2_reader/v1/schema.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <set>

namespace cs2::synchronization::v1 {
namespace {
using wire::Writer;
Writer clock(const ServerIdentity& v) {
    Writer w;
    (void)w.blob(1, v.clock_domain);
    (void)w.fixed64(2, v.qpc_frequency);
    return w;
}
void unique(std::set<unsigned>& seen, unsigned number) {
    if (!seen.insert(number).second)
        throw wire::Error("duplicate control field");
}
} // namespace
ClientRequest decode_request(std::span<const std::byte> bytes, const InstanceId& instance) {
    wire::Reader envelope_reader(bytes);
    wire::Field field;
    std::set<unsigned> seen;
    unsigned version = 0, body = 0;
    std::span<const std::byte> payload;
    while (envelope_reader.next(field)) {
        unique(seen, field.number);
        switch (field.number) {
        case 1:
            field.require(0);
            if (field.scalar != 1)
                throw wire::Error("unsupported envelope version");
            version = 1;
            break;
        case 2:
            field.require(2);
            if (!field.bytes.empty() &&
                !std::equal(field.bytes.begin(), field.bytes.end(), instance.begin(), instance.end()))
                throw wire::Error("reader instance mismatch");
            break;
        case 3:
            field.require(1);
            if (field.scalar != 0)
                throw wire::Error("client cannot issue server event sequence");
            break;
        case 11:
        case 15:
            field.require(2);
            if (body)
                throw wire::Error("ambiguous control body");
            body = field.number;
            payload = field.bytes;
            break;
        default:
            throw wire::Error("unsupported client envelope field or direction");
        }
    }
    if (version != 1 || !body)
        throw wire::Error("missing envelope version or body");
    ClientRequest result;
    result.kind = body == 11 ? ClientRequest::Kind::Subscribe : ClientRequest::Kind::ClockQuery;
    wire::Reader request_reader(payload);
    seen.clear();
    while (request_reader.next(field)) {
        unique(seen, field.number);
        if (field.number == 1) {
            field.require(1);
            result.request_id = field.scalar;
        } else if (body == 11 && field.number == 2) {
            field.require(0);
            if (field.scalar > std::numeric_limits<std::uint32_t>::max())
                throw wire::Error("protocol version overflow");
            result.version = static_cast<std::uint32_t>(field.scalar);
        } else if (body == 11 && field.number == 3) {
            field.require(2);
            result.schema.assign(field.bytes.begin(), field.bytes.end());
        } else if (body == 11 && field.number == 4) {
            field.require(1);
            result.maximum_publish_hz = std::bit_cast<double>(field.scalar);
        } else
            throw wire::Error("unsupported request field");
    }
    if (!result.request_id)
        throw wire::Error("request id must be nonzero");
    return result;
}
Message hello(const ServerIdentity& v) {
    Writer w;
    w.uint32(1, 1);
    (void)w.blob(2, cs2_reader::v1::schema_sha256);
    w.uint32(3, v.hyperv ? 2U : 1U);
    w.message(4, clock(v));
    w.real(5, v.capture_hz);
    w.boolean(6, true);
    w.boolean(7, true);
    w.boolean(8, false);
    w.boolean(9, v.verified_bone_semantics);
    w.boolean(11, true);
    w.boolean(12, true);
    w.boolean(13, v.verified_bone_semantics);
    w.boolean(14, true);
    w.boolean(10, true);
    return {10, std::move(w.bytes), {}};
}
Message subscribed(std::uint64_t request, double capture_hz, double publish_hz) {
    Writer w;
    (void)w.fixed64(1, request);
    w.real(2, capture_hz);
    w.real(3, publish_hz);
    return {12, std::move(w.bytes), {}};
}
Message status(const SourceStatus& v) {
    Writer w;
    w.uint32(1, static_cast<unsigned>(v.availability));
    if (v.epoch)
        (void)w.fixed64(2, *v.epoch);
    if (v.last_success)
        (void)w.fixed64(3, *v.last_success);
    w.text(4, v.reason);
    return {14, std::move(w.bytes), {}};
}
Message reject(std::uint64_t request, std::string_view code, std::string_view detail) {
    Writer w;
    (void)w.fixed64(1, request);
    w.text(2, code);
    w.text(3, detail);
    return {17, std::move(w.bytes), {}};
}
Message heartbeat(std::optional<std::uint64_t> last_success, std::uint64_t coalesced) {
    Writer w;
    if (last_success)
        (void)w.fixed64(1, *last_success);
    (void)w.fixed64(2, coalesced);
    return {18, std::move(w.bytes), {}};
}
Message clock_sample(std::uint64_t request, std::uint64_t received, const ServerIdentity& v) {
    Writer w;
    (void)w.fixed64(1, request);
    w.message(2, clock(v));
    (void)w.fixed64(3, received);
    const auto offset = w.fixed64(4, 0);
    return {16, std::move(w.bytes), offset};
}
void validate_snapshot_size(const ServerIdentity& v, std::size_t size) {
    Writer prefix;
    prefix.uint32(1, 1);
    (void)prefix.blob(2, v.instance);
    (void)prefix.fixed64(3, 1);
    (void)prefix.fixed64(4, 0);
    std::size_t length_bytes = 1;
    for (auto remaining = size; remaining >= 128; remaining >>= 7)
        ++length_bytes;
    // Snapshot的tag为13，键占一字节；上限检查必须包含整个Envelope，而非仅Snapshot正文。
    const auto overhead = prefix.bytes.size() + 1 + length_bytes;
    if (overhead > wire::body_limit || size > wire::body_limit - overhead)
        throw wire::Error("snapshot envelope exceeds 16 MiB resource limit");
}
EncodedRecord envelope(const ServerIdentity& v, std::uint64_t sequence, const Message& message) {
    Writer w;
    w.uint32(1, 1);
    (void)w.blob(2, v.instance);
    (void)w.fixed64(3, sequence);
    const auto stamp = w.fixed64(4, 0);
    const auto body = w.blob(message.field, message.body);
    EncodedRecord result;
    result.bytes = cs2_reader::v1::make_record(w.bytes);
    result.stamp_offsets.push_back(stamp + 4);
    if (message.clock_stamp_offset)
        result.stamp_offsets.push_back(body + *message.clock_stamp_offset + 4);
    return result;
}
} // namespace cs2::synchronization::v1
