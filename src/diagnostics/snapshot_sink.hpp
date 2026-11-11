#pragma once
#include "model/snapshot.hpp"
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
namespace cs2::diagnostics {
class SnapshotSink {
  public:
    explicit SnapshotSink(const std::filesystem::path&);
    ~SnapshotSink();
    void submit(std::shared_ptr<const model::Snapshot> snapshot);
    std::uint64_t coalesced() const;
    std::string error() const;

  private:
    void work();
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::shared_ptr<const model::Snapshot> latest_;
    bool stopping_ = false;
    std::uint64_t coalesced_ = 0;
    std::string error_;
    std::ofstream file_;
    std::thread thread_;
};
} // namespace cs2::diagnostics
