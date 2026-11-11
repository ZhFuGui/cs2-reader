#include "diagnostics/snapshot_sink.hpp"
#include "synchronization/json_snapshot.hpp"
#include <stdexcept>
namespace cs2::diagnostics {
SnapshotSink::SnapshotSink(const std::filesystem::path& path) {
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    file_.open(path, std::ios::binary | std::ios::trunc);
    if (!file_)
        throw std::runtime_error("Cannot create the requested snapshot output");
    thread_ = std::thread([this] { work(); });
}
SnapshotSink::~SnapshotSink() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    ready_.notify_one();
    if (thread_.joinable())
        thread_.join();
}
void SnapshotSink::submit(std::shared_ptr<const model::Snapshot> snapshot) {
    {
        std::lock_guard lock(mutex_);
        if (latest_)
            ++coalesced_;
        latest_ = std::move(snapshot);
    }
    ready_.notify_one();
}
std::uint64_t SnapshotSink::coalesced() const {
    std::lock_guard lock(mutex_);
    return coalesced_;
}
std::string SnapshotSink::error() const {
    std::lock_guard lock(mutex_);
    return error_;
}
void SnapshotSink::work() {
    for (;;) {
        std::shared_ptr<const model::Snapshot> snapshot;
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [this] { return stopping_ || latest_; });
            if (!latest_ && stopping_)
                return;
            snapshot = std::move(latest_);
            latest_.reset();
        }
        try {
            file_ << synchronization::snapshot_json(*snapshot) << '\n';
            file_.flush();
            if (!file_)
                throw std::runtime_error("Snapshot output I/O failed");
        } catch (const std::exception& e) {
            std::lock_guard lock(mutex_);
            error_ = e.what();
        }
    }
}
} // namespace cs2::diagnostics
