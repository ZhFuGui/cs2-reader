#pragma once
#include "capture/clock.hpp"
#include "capture/frequency_control.hpp"
#include "model/source_status.hpp"
#include <memory>
#include <string>

namespace cs2::model {
struct Snapshot;
}

namespace cs2::synchronization::v2 {
struct ServiceOptions {
    std::uint16_t port = 13202;
    std::size_t maximum_connections = 8;
    double capture_hz = 90;
    bool bones_enabled = true;
    std::string bind_address = "127.0.0.1";
};
struct ServiceStats {
    std::uint64_t encoded{}, sent{}, pending_coalesced{}, encoder_coalesced{}, connected{};
    std::optional<model::Fault> encoding_fault, network_fault;
};
// 采集只交接不可变模型；一个产品编码线程和一个共享 TCP 网络线程分别拥有后续工作。
class SubscriptionService final {
  public:
    SubscriptionService(const ServiceOptions&, const capture::IClock&, capture::FrequencyControl* = nullptr);
    ~SubscriptionService();
    SubscriptionService(const SubscriptionService&) = delete;
    SubscriptionService& operator=(const SubscriptionService&) = delete;
    void submit(std::shared_ptr<const model::Snapshot>);
    void unavailable(model::Availability, model::ReasonCode, std::optional<std::uint64_t> epoch = {});
    ServiceStats stats() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace cs2::synchronization::v2
