#pragma once
#include "capture/clock.hpp"
#include "memory/session.hpp"
#include "model/snapshot.hpp"
#include <exception>
#include <memory>

namespace cs2::capture {
class IncompatibleSource final : public std::exception {};

// 一次绑定拥有一个采集器；调用方必须先销毁采集器，再释放其引用的会话。
class ISource {
  public:
    virtual ~ISource() = default;
    virtual std::uint32_t build() const noexcept = 0;
    virtual model::Snapshot capture(std::uint64_t sequence, std::uint64_t epoch) = 0;
    virtual void invalidate() noexcept = 0;
};
class ISourceFactory {
  public:
    virtual ~ISourceFactory() = default;
    virtual std::unique_ptr<ISource> open(const memory::ISession&, const IClock&) const = 0;
};
} // namespace cs2::capture
