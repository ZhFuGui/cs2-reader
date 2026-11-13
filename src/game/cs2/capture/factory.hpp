#pragma once
#include "capture/source.hpp"

namespace cs2::game {
class SourceFactory final : public capture::ISourceFactory {
  public:
    SourceFactory(std::size_t workers, bool bones) : workers_(workers), bones_(bones) {}
    std::unique_ptr<capture::ISource> open(const memory::ISession&, const capture::IClock&) const override;

  private:
    const std::size_t workers_;
    const bool bones_;
};
} // namespace cs2::game
