#pragma once
#include "runtime/control.hpp"
#include <memory>
#include <string_view>

namespace cs2::platform::windows {
class RunControl final : public runtime::IRunControl {
  public:
    RunControl(std::wstring_view stop_event, bool console_controls);
    ~RunControl();
    bool stopping() const override;
    void wait_until(std::chrono::steady_clock::time_point, capture::FrequencyControl* = nullptr,
                    std::uint64_t revision = 0) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace cs2::platform::windows
