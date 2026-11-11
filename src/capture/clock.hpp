#pragma once
#include <cstdint>
namespace cs2::capture {
class IClock {
  public:
    virtual ~IClock() = default;
    virtual std::uint64_t now() const noexcept = 0;
    virtual std::uint64_t frequency() const noexcept = 0;
};
} // namespace cs2::capture
