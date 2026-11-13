#pragma once
#include "model/snapshot.hpp"
#include "model/source_status.hpp"
#include "synchronization/v1/wire.hpp"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string_view>

namespace cs2::synchronization::v1 {
struct EncodedSnapshot {
    wire::Bytes bytes;
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
    std::string error;
};
// 唯一跨线程交接点：一个最新待编码输入和一个已编码结果，不拥有线程或 socket。
// take/commit 由一个编码所有者顺序调用；采集、失效和视图读取可以并发。
class Publication final {
  public:
    bool submit(std::shared_ptr<const model::Snapshot>);
    bool invalidate(model::Availability, std::string reason, std::optional<std::uint64_t> epoch);
    std::optional<EncodingTask> take(); // 空闲时等待；stop 后返回缺失。
    bool commit(std::shared_ptr<const EncodedSnapshot>);
    bool encoding_failed(std::uint64_t token, std::string error);
    void failure(std::string error);
    void clear_error(std::string_view prefix);
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
    std::string error_;
};
} // namespace cs2::synchronization::v1
