#pragma once
#include <stdexcept>

namespace cs2::memory {
// 来源暂时不可读或观察不一致；丢弃本次结果，保留进程并等待重新读取。
// 配置、版本不匹配和程序内部约定错误不使用此类型。
class ReadUnavailable : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};
} // namespace cs2::memory
