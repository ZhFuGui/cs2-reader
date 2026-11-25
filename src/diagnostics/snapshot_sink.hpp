#pragma once
#include "model/snapshot.hpp"
#include <condition_variable>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
namespace cs2::diagnostics {
class SnapshotOutputFailed final : public std::exception {
  public:
    explicit SnapshotOutputFailed(std::optional<std::uint32_t> code = {}) noexcept : os_code(code) {}
    const std::optional<std::uint32_t> os_code;
};

class SnapshotSink {
  public:
    explicit SnapshotSink(const std::filesystem::path&);
    ~SnapshotSink();
    void submit(std::shared_ptr<const model::Snapshot> snapshot);
    std::uint64_t coalesced() const;
    bool failed() const;

  private:
    void work();
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::shared_ptr<const model::Snapshot> latest_;
    bool stopping_ = false;
    std::uint64_t coalesced_ = 0;
    bool failed_ = false;
    std::ofstream file_;
    std::thread thread_;
};
} // namespace cs2::diagnostics
