#include "memory/block_reader.hpp"
#include "memory/session.hpp"
#include <array>
#include <set>
#include <stdexcept>

namespace cs2::memory {
namespace {
template <class T> T required(IReadChannel& channel, Address address) {
    auto value = read_value<T>(channel, address);
    if (!value)
        throw std::runtime_error("Remote module list is unreadable");
    return *value;
}
struct Unicode {
    std::uint16_t length, maximum;
    std::uint32_t pad;
    Address data;
};
} // namespace
std::vector<Module> read_modules(IReadChannel& channel, Address peb) {
    const auto ldr = required<Address>(channel, peb + 0x18);
    if (!user_range(ldr, 0x30))
        throw std::runtime_error("Invalid PEB loader");
    const auto head = ldr + 0x10;
    auto link = required<Address>(channel, head);
    std::set<Address> seen;
    std::vector<Module> modules;
    while (link != head) {
        if (!user_range(link, 0x68) || !seen.insert(link).second)
            throw std::runtime_error("Remote loader list is inconsistent");
        std::array<std::byte, 0x68> entry{};
        if (!channel.read(link, entry.data(), entry.size()).complete(entry.size()))
            throw std::runtime_error("Remote loader entry is unreadable");
        auto copy = [&]<class T>(std::size_t offset) {
            T value;
            std::memcpy(&value, entry.data() + offset, sizeof(T));
            return value;
        };
        auto base = copy.template operator()<Address>(0x30);
        auto size = copy.template operator()<std::uint32_t>(0x40);
        auto name = copy.template operator()<Unicode>(0x58);
        if (!name.length || name.length % 2 || name.length > name.maximum || !user_range(name.data, name.length) ||
            !user_range(base, size))
            throw std::runtime_error("Invalid loader module descriptor");
        Module module;
        module.base = base;
        module.size = size;
        module.name.resize(name.length / 2);
        if (!channel.read(name.data, module.name.data(), name.length).complete(name.length))
            throw std::runtime_error("Remote module name is unreadable");
        const auto mz = required<std::uint16_t>(channel, base);
        const auto offset = required<std::int32_t>(channel, base + 0x3C);
        if (mz != 0x5A4D || offset < 0x40 || static_cast<std::uint32_t>(offset) > size || size - offset < 0x78)
            throw std::runtime_error("Invalid module DOS image");
        const auto nt = base + offset;
        if (required<std::uint32_t>(channel, nt) != 0x4550 || required<std::uint16_t>(channel, nt + 4) != 0x8664 ||
            required<std::uint16_t>(channel, nt + 0x18) != 0x20B || required<std::uint32_t>(channel, nt + 0x50) != size)
            throw std::runtime_error("Module is not a consistent x64 PE image");
        module.timestamp = required<std::uint32_t>(channel, nt + 8);
        if (required<Address>(channel, link) != copy.template operator()<Address>(0))
            throw std::runtime_error("Loader entry changed during enumeration");
        modules.push_back(std::move(module));
        link = copy.template operator()<Address>(0);
    }
    return modules;
}
} // namespace cs2::memory
