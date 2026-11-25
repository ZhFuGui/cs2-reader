#pragma once
#include "model/source_status.hpp"
#include "synchronization/v2/projection.hpp"
#include <optional>
#include <string>

namespace cs2::synchronization::v2 {
struct ServerIdentity {
    InstanceId instance{}, clock_domain{};
    std::uint64_t qpc_frequency{};
    double capture_hz{};
    bool verified_bone_semantics{};
};
using model::Availability;
using model::ReasonCode;
using model::SourceStatus;
struct ClientRequest {
    enum class Kind { Subscribe, ClockQuery } kind{};
    std::uint64_t request_id{};
    std::uint32_t version{};
    wire::Bytes schema;
    std::optional<double> maximum_publish_hz;
};
struct Message {
    unsigned field{};
    wire::Bytes body;
    std::optional<std::size_t> clock_stamp_offset;
};
// 完整 protobuf 正文；长度头由共享 TCP 拥有。固定宽度字段在首次发送前原位盖戳。
struct EncodedRecord {
    wire::Bytes body;
    std::size_t sequence_offset{};
    std::vector<std::size_t> stamp_offsets;
};
ClientRequest decode_request(std::span<const std::byte>, const InstanceId&);
Message hello(const ServerIdentity&);
Message subscribed(std::uint64_t request, double capture_hz, double publish_hz);
Message status(const SourceStatus&);
Message reject(std::uint64_t request, ReasonCode);
Message heartbeat(std::optional<std::uint64_t> last_success, std::uint64_t coalesced);
Message clock_sample(std::uint64_t request, std::uint64_t received, const ServerIdentity&);
void validate_snapshot_size(const ServerIdentity&, std::size_t payload_size);
EncodedRecord envelope(const ServerIdentity&, std::uint64_t event_sequence, const Message&);
} // namespace cs2::synchronization::v2
