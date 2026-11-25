#include "memory/read_channel.hpp"
#include <readers/memory/read_channel.hpp>

namespace cs2::memory {
namespace {
// 本地观测通道的默认行为也使用共享计划解释器，不维护第二套读取规则。
class SharedFallback final : public readers::memory::ReadChannel {
  public:
    explicit SharedFallback(IReadChannel& source) : source_(source) {}
    readers::memory::ReadResult read(Address address, void* output, std::size_t bytes) noexcept override {
        return source_.read(address, output, bytes);
    }
    readers::memory::ReadMetrics take_metrics() noexcept override { return {}; }
    readers::memory::Capabilities capabilities() const noexcept override { return {}; }

  private:
    IReadChannel& source_;
};
} // namespace
bool canonical_range(Address address, std::size_t bytes) noexcept {
    if (!address || (bytes && bytes - 1 > UINT64_MAX - address))
        return false;
    const auto top = address >> 47;
    return (top == 0 || top == 0x1FFFF) && ((address + (bytes ? bytes - 1 : 0)) >> 47) == top;
}
std::optional<Address> relative_address(Address base, std::uint64_t offset, std::size_t bytes) noexcept {
    Address address;
    if (offset >> 63) {
        const auto magnitude = ~offset + 1;
        if (magnitude > base)
            return {};
        address = base - magnitude;
    } else {
        if (offset > UINT64_MAX - base)
            return {};
        address = base + offset;
    }
    return canonical_range(address, bytes) ? std::optional(address) : std::nullopt;
}
void IReadChannel::read_many(std::span<ReadRequest> requests) noexcept {
    SharedFallback(*this).read_many(requests);
}
void IReadChannel::read_dependent(std::span<DependentRead> requests) noexcept {
    SharedFallback(*this).read_dependent(requests);
}
} // namespace cs2::memory
