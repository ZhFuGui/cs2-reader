#include "capture/worker_pool.hpp"
#include <algorithm>
#include <stdexcept>
namespace cs2::capture {
WorkerPool::WorkerPool(const memory::ISession& session, std::size_t count) {
    if (!count)
        throw std::runtime_error("Worker count must be positive");
    workers_.reserve(count);
    try {
        for (std::size_t i = 0; i < count; ++i) {
            auto worker = std::make_unique<Worker>();
            worker->source = session.open_reader();
            if (!worker->source)
                throw std::runtime_error("Backend did not provide a reader");
            worker->reader = std::make_unique<memory::MeteredChannel>(*worker->source);
            workers_.push_back(std::move(worker));
        }
        for (auto& worker : workers_)
            worker->thread = std::thread([this, pointer = worker.get()] { work(*pointer); });
    } catch (...) {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        for (auto& worker : workers_)
            if (worker->thread.joinable())
                worker->thread.join();
        throw;
    }
}
WorkerPool::~WorkerPool() {
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
    const std::lock_guard owner(run_mutex_);
    {
        std::lock_guard lock(mutex_);
        task_ = std::move(task);
        count_ = count;
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
            const auto first = next_.fetch_add(8, std::memory_order_relaxed);
            if (first >= count_)
                break;
            for (auto i = first; i < std::min(first + 8, count_); ++i) {
                try {
                    task_(*worker.reader, i);
                } catch (...) {
                    const std::lock_guard lock(mutex_);
                    if (!error_)
                        error_ = std::current_exception();
                }
            }
        }
        {
            std::lock_guard lock(mutex_);
            if (--remaining_ == 0)
                finished_.notify_one();
        }
    }
}
memory::ReadMetrics WorkerPool::take_metrics() {
    const std::lock_guard owner(run_mutex_);
    memory::ReadMetrics result;
    for (auto& worker : workers_)
        result += worker->reader->take_metrics();
    return result;
}
} // namespace cs2::capture
