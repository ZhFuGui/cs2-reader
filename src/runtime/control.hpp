#pragma once
#include "capture/frequency_control.hpp"

namespace cs2::runtime {
// 停止来源由宿主适配；运行循环不认识控制台、句柄或 DLL 的加载方式。
class IRunControl {
  public:
    virtual ~IRunControl() = default;
    virtual bool stopping() const = 0;
    virtual void wait_until(std::chrono::steady_clock::time_point, capture::FrequencyControl* frequency = nullptr,
                            std::uint64_t revision = 0) = 0;
};
} // namespace cs2::runtime
