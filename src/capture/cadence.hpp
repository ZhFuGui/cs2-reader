#pragma once
#include "capture/frequency_control.hpp"

namespace cs2::capture {
// 固定时间线只统计错过的机会，不积压补采；等待机制由宿主控制。
class Cadence final {
  public:
    using Clock = std::chrono::steady_clock;
    Cadence(Frequency rate, Clock::time_point now) : rate_(rate), next_(now), period_(period(rate)) {}
    void configure(Frequency rate, Clock::time_point now) {
        if (rate.revision == rate_.revision)
            return;
        const auto interval = period(rate);
        rate_ = rate;
        period_ = interval;
        next_ = now;
    }
    std::uint64_t advance(Clock::time_point finished) {
        next_ += period_;
        if (finished < next_ + period_)
            return 0;
        const auto skipped = (finished - next_) / period_;
        next_ += period_ * skipped;
        return static_cast<std::uint64_t>(skipped);
    }
    Frequency rate() const noexcept { return rate_; }
    Clock::time_point next() const noexcept { return next_; }

  private:
    static std::chrono::nanoseconds period(Frequency value) {
        if (!std::isfinite(value.hz) || value.hz < 1 || value.hz > 300)
            throw std::invalid_argument("Cadence frequency must be within 1..300 Hz");
        return std::chrono::nanoseconds(static_cast<std::int64_t>(1e9 / value.hz));
    }
    Frequency rate_;
    Clock::time_point next_;
    std::chrono::nanoseconds period_;
};
} // namespace cs2::capture
