#pragma once
#include "model/source_status.hpp"
#include "synchronization/v2/protocol.hpp"
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace cs2::model {
struct Snapshot;
} // namespace cs2::model

namespace cs2::synchronization::v2 {
struct EncodedSnapshot {
    EncodedRecord record;
    std::uint64_t token{}, sequence{}, finished_qpc{};
    double capture_hz{};
};
struct EncodingTask {
    std::shared_ptr<const model::Snapshot> snapshot;
    std::uint64_t token{};
};
struct PublicationView {
    model::SourceStatus status;
    std::uint64_t token{}, status_revision{}, invalidation_serial{};
    std::shared_ptr<const EncodedSnapshot> packet;
};
struct PublicationStats {
    std::uint64_t encoded{}, coalesced{};
    std::optional<model::Fault> encoding_fault, network_fault;
};
// 唯一跨线程交接点：一个最新待编码输入和一个已编码结果，不拥有线程或 socket。
// take/commit 由一个编码所有者顺序调用；采集、失效和视图读取可以并发。
class Publication final {
  public:
    bool submit(std::shared_ptr<const model::Snapshot>);
    bool invalidate(model::Availability, model::ReasonCode, std::optional<std::uint64_t> epoch);
    // 限时等待使编码所有者也能处理已安全退出的 TCP 服务恢复；不执行游戏读取。
    std::optional<EncodingTask> take(std::chrono::milliseconds timeout = std::chrono::milliseconds::max());
    bool commit(std::shared_ptr<const EncodedSnapshot>);
    bool encoding_failed(std::uint64_t token);
    void network_failed(std::optional<std::uint32_t> os_code = {});
    void network_recovered();
    void stop();
    PublicationView view() const;
    PublicationStats stats() const;

  private:
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    PublicationView state_;
    std::optional<EncodingTask> input_;
    bool stopping_ = false;
    std::uint64_t encoded_ = 0, coalesced_ = 0;
    std::optional<model::Fault> encoding_fault_, network_fault_;
};
} // namespace cs2::synchronization::v2
