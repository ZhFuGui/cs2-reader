#pragma once
#include "synchronization/v1/projection.hpp"
#include "transport/record.hpp"
#include <optional>
#include <string>

namespace cs2::synchronization::v1 {
struct ServerIdentity {
    InstanceId instance{}, clock_domain{};
    std::uint64_t qpc_frequency{};
    double capture_hz{};
    bool verified_bone_semantics{};
};
enum class Availability : unsigned { Waiting = 1, Ready = 2, Unavailable = 3, Stopping = 4 };
struct SourceStatus {
    Availability availability = Availability::Waiting;
    std::optional<std::uint64_t> epoch, last_success;
    std::string reason = "WAITING_FOR_PROCESS";
};
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
using EncodedRecord = transport::Record;
ClientRequest decode_request(std::span<const std::byte>, const InstanceId&);
Message hello(const ServerIdentity&);
Message subscribed(std::uint64_t request, double capture_hz, double publish_hz);
Message status(const SourceStatus&);
Message reject(std::uint64_t request, std::string_view code, std::string_view detail);
Message heartbeat(std::optional<std::uint64_t> last_success, std::uint64_t coalesced);
Message clock_sample(std::uint64_t request, std::uint64_t received, const ServerIdentity&);
void validate_snapshot_size(const ServerIdentity&, std::size_t payload_size);
EncodedRecord envelope(const ServerIdentity&, std::uint64_t event_sequence, const Message&);
} // namespace cs2::synchronization::v1
