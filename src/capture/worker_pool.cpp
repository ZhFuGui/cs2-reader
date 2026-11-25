#include "capture/worker_pool.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>
namespace cs2::capture {
WorkerPool::Worker::Worker(std::unique_ptr<memory::IReadChannel> channel, std::size_t worker_index)
    : index(worker_index), source(std::move(channel)), reader(*source) {}

WorkerPool::WorkerPool(const memory::ISession& session, std::size_t count) {
    if (!count)
        throw std::runtime_error("Worker count must be positive");
    workers_.reserve(count);
    try {
        for (std::size_t i = 0; i < count; ++i) {
            auto channel = session.open_reader();
            if (!channel)
                throw std::runtime_error("Backend did not provide a reader");
            workers_.push_back(std::make_unique<Worker>(std::move(channel), i));
        }
        for (auto& worker : workers_)
            worker->thread = std::thread([this, pointer = worker.get()] { work(*pointer); });
    } catch (...) {
        stop();
        throw;
    }
}
WorkerPool::~WorkerPool() { stop(); }

void WorkerPool::stop() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    ready_.notify_all();
    for (auto& worker : workers_)
        if (worker->thread.joinable())
            worker->thread.join();
}
void WorkerPool::run(std::size_t count, std::function<void(memory::IReadChannel&, std::size_t)> task) {
    run_ranges(count, 1, [task = std::move(task)](auto& channel, auto first, auto) { task(channel, first); });
}
void WorkerPool::run_ranges(std::size_t count, std::size_t grain,
                            std::function<void(memory::IReadChannel&, std::size_t, std::size_t)> task) {
    run_ranges_indexed(count, grain, [task = std::move(task)](auto& reader, auto, auto first, auto last) {
        task(reader, first, last);
    });
}
void WorkerPool::run_ranges_indexed(
    std::size_t count, std::size_t grain,
    std::function<void(memory::IReadChannel&, std::size_t, std::size_t, std::size_t)> task) {
    if (!grain)
        throw std::invalid_argument("Worker group size must be positive");
    const std::lock_guard owner(run_mutex_);
    {
        std::lock_guard lock(mutex_);
        task_ = std::move(task);
        count_ = count;
        grain_ = grain;
        next_ = 0;
        error_ = {};
        remaining_ = workers_.size();
        ++generation_;
    }
    ready_.notify_all();
    std::unique_lock lock(mutex_);
    finished_.wait(lock, [this] { return remaining_ == 0; });
    task_ = {};
    if (error_)
        std::rethrow_exception(error_);
}
void WorkerPool::work(Worker& worker) {
    std::uint64_t observed = 0;
    for (;;) {
        {
            std::unique_lock lock(mutex_);
            ready_.wait(lock, [&] { return stopping_ || observed != generation_; });
            if (stopping_)
                return;
            observed = generation_;
        }
        for (;;) {
            const auto first = next_.fetch_add(grain_, std::memory_order_relaxed);
            if (first >= count_)
                break;
            try {
                task_(worker.reader, worker.index, first, first + std::min(grain_, count_ - first));
            } catch (...) {
                const std::lock_guard lock(mutex_);
                if (!error_)
                    error_ = std::current_exception();
            }
        }
        {
            std::lock_guard lock(mutex_);
            if (--remaining_ == 0)
                finished_.notify_one();
        }
    }
}
diagnostics::ReadMetrics WorkerPool::take_metrics() {
    const std::lock_guard owner(run_mutex_);
    diagnostics::ReadMetrics result;
    for (auto& worker : workers_)
        result += worker->reader.take_metrics();
    return result;
}
} // namespace cs2::capture
