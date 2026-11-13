#pragma once
#include "app/options.hpp"
#include "capture/clock.hpp"
#include "capture/frequency_control.hpp"
#include "runtime/output.hpp"

namespace cs2::diagnostics {
class SnapshotSink;
}
namespace cs2::synchronization::v1 {
class SubscriptionService;
}
namespace cs2::app {
// 发布扇出由组合层拥有；运行循环不识别具体协议或可选文件输出。
class Outputs final : public runtime::IOutput {
  public:
    Outputs(const Options&, const capture::IClock&, capture::FrequencyControl&);
    ~Outputs();
    void submit(std::shared_ptr<const model::Snapshot>) override;
    void unavailable(model::Availability, std::string, std::optional<std::uint64_t>) override;
    void report(std::ostream&) const override;
    std::optional<std::uint64_t> coalesced() const override;

  private:
    std::unique_ptr<synchronization::v1::SubscriptionService> service_;
    std::unique_ptr<diagnostics::SnapshotSink> sink_;
};
} // namespace cs2::app
