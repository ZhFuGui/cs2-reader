#pragma once
#include "capture/clock.hpp"
#include <readers/clock.hpp>
#include <stdexcept>
namespace cs2::platform::windows {
class QpcClock final : public capture::IClock {
  public:
    QpcClock() {
        frequency_ = source_.frequency();
        if (!frequency_)
            throw std::runtime_error("QPC is unavailable");
    }
    std::uint64_t now() const noexcept override { return source_.now(); }
    std::uint64_t frequency() const noexcept override { return frequency_; }

  private:
    readers::QpcClock source_;
    std::uint64_t frequency_;
};
} // namespace cs2::platform::windows
