#pragma once
#include "memory/session.hpp"
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
namespace cs2::capture {
class WorkerPool {
  public:
    explicit WorkerPool(const memory::ISession&, std::size_t workers);
    ~WorkerPool();
    WorkerPool(const WorkerPool&) = delete;
    // 回调只写自己对应的结果槽位；一个组结束后才允许下一组复用 worker 通道。
    void run(std::size_t count, std::function<void(memory::IReadChannel&, std::size_t)> task);
    memory::ReadMetrics take_metrics();
    std::size_t size() const noexcept { return workers_.size(); }

  private:
    struct Worker {
        std::unique_ptr<memory::IReadChannel> source;
        std::unique_ptr<memory::MeteredChannel> reader;
        std::thread thread;
    };
    void work(Worker&);
    std::vector<std::unique_ptr<Worker>> workers_;
    std::mutex mutex_, run_mutex_;
    std::condition_variable ready_, finished_;
    std::function<void(memory::IReadChannel&, std::size_t)> task_;
    std::atomic_size_t next_{0};
    std::size_t count_ = 0, remaining_ = 0;
    std::uint64_t generation_ = 0;
    bool stopping_ = false;
    std::exception_ptr error_;
};
} // namespace cs2::capture
