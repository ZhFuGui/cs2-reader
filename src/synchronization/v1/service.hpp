#pragma once
#include "capture/clock.hpp"
#include "capture/frequency_control.hpp"
#include "synchronization/v1/protocol.hpp"
#include <memory>
#include <string>

namespace cs2::synchronization::v1 {
struct ServiceOptions {
    std::uint16_t port = 13202;
    std::size_t maximum_connections = 8;
    double capture_hz = 90;
    bool bones_enabled = true;
    std::string bind_address = "127.0.0.1";
};
struct ServiceStats {
    std::uint64_t encoded{}, sent{}, pending_coalesced{}, encoder_coalesced{}, connected{};
    std::string error;
};
// 采集调用只交接不可变模型；编码和socket由两个独立线程拥有。
class SubscriptionService final {
  public:
    SubscriptionService(const ServiceOptions&, const capture::IClock&, capture::FrequencyControl* = nullptr);
    ~SubscriptionService();
    SubscriptionService(const SubscriptionService&) = delete;
    SubscriptionService& operator=(const SubscriptionService&) = delete;
    void submit(std::shared_ptr<const model::Snapshot>);
    void unavailable(Availability, std::string reason, std::optional<std::uint64_t> epoch = {});
    ServiceStats stats() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace cs2::synchronization::v1
