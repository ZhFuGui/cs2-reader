#pragma once
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <stdexcept>

namespace cs2::capture {
struct Frequency {
    double hz;
    std::uint64_t revision;
};
// 协议只提出采集目标；采集所有者在开批时读取版本，等待期间可被新目标唤醒。
class FrequencyControl final {
  public:
    explicit FrequencyControl(double hz) : current_{hz, 1} { validate(hz); }
    Frequency current() const {
        std::lock_guard lock(mutex_);
        return current_;
    }
    void set(double hz) {
        validate(hz);
        {
            std::lock_guard lock(mutex_);
            if (current_.hz == hz)
                return;
            current_ = {hz, current_.revision + 1};
        }
        changed_.notify_all();
    }
    bool wait_until(std::chrono::steady_clock::time_point deadline, std::uint64_t expected_revision) {
        std::unique_lock lock(mutex_);
        return !changed_.wait_until(lock, deadline, [&] { return current_.revision != expected_revision; });
    }

  private:
    static void validate(double hz) {
        if (!std::isfinite(hz) || hz < 1 || hz > 300)
            throw std::runtime_error("Frequency must be within 1..300 Hz");
    }
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    Frequency current_;
};
} // namespace cs2::capture
