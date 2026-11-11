#pragma once
#include "capture/clock.hpp"
#include "platform/windows/native_api.hpp"
#include <stdexcept>
namespace cs2::platform::windows {
class QpcClock final : public capture::IClock {
  public:
    QpcClock() {
        LARGE_INTEGER value{};
        if (!QueryPerformanceFrequency(&value) || value.QuadPart <= 0)
            throw std::runtime_error("QPC is unavailable");
        frequency_ = value.QuadPart;
    }
    std::uint64_t now() const noexcept override {
        LARGE_INTEGER value{};
        QueryPerformanceCounter(&value);
        return value.QuadPart;
    }
    std::uint64_t frequency() const noexcept override { return frequency_; }

  private:
    std::uint64_t frequency_;
};
} // namespace cs2::platform::windows
