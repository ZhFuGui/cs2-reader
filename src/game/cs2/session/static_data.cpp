#include "game/cs2/session/static_data.hpp"
#include "memory/read_plan.hpp"
#include <array>
#include <algorithm>

namespace cs2::game {
namespace {
template <class T, std::size_t N> T field(const std::array<std::byte, N>& bytes, std::size_t offset) {
    T result;
    std::memcpy(&result, bytes.data() + offset, sizeof(T));
    return result;
}
std::vector<StaticData::Range> read_regions(memory::IReadChannel& reader, const memory::Module& module) {
    std::array<std::byte, 64> dos{};
    if (!module.is_x64() || module.size < dos.size() ||
        !reader.read(module.base, dos.data(), dos.size()).complete(dos.size()) ||
        field<std::uint16_t>(dos, 0) != 0x5a4d)
        return {};
    const auto offset = field<std::int32_t>(dos, 0x3c);
    std::array<std::byte, 24> nt{};
    if (offset < 64 || static_cast<std::uint32_t>(offset) > module.size || module.size - offset < nt.size() ||
        !reader.read(module.base + offset, nt.data(), nt.size()).complete(nt.size()) ||
        field<std::uint32_t>(nt, 0) != 0x4550 || field<std::uint16_t>(nt, 4) != module.machine ||
        field<std::uint32_t>(nt, 8) != module.timestamp)
        return {};
    const auto count = field<std::uint16_t>(nt, 6);
    const auto optional_size = field<std::uint16_t>(nt, 20);
    const auto table_offset = static_cast<std::uint64_t>(offset) + nt.size() + optional_size;
    const auto table_bytes = static_cast<std::size_t>(count) * 40;
    if (!count || optional_size < 64 || table_offset > module.size || table_bytes > module.size - table_offset)
        return {};
    std::array<std::byte, 64> optional{};
    if (!reader.read(module.base + offset + nt.size(), optional.data(), optional.size()).complete(optional.size()) ||
        field<std::uint16_t>(optional, 0) != module.optional_magic ||
        field<std::uint32_t>(optional, 56) != module.size)
        return {};
    std::vector<std::array<std::byte, 40>> sections(count);
    if (!reader.read(module.base + table_offset, sections.data(), table_bytes).complete(table_bytes))
        return {};
    std::vector<StaticData::Range> result;
    std::vector<StaticData::Range> all;
    for (const auto& section : sections) {
        const auto size = field<std::uint32_t>(section, 8), rva = field<std::uint32_t>(section, 12);
        const auto flags = field<std::uint32_t>(section, 36);
        if (size && (rva >= module.size || size > module.size - rva))
            return {};
        if (size)
            all.emplace_back(module.base + rva, size);
        // 模块的正常装载生命周期约定，不声称监测任意同址热补丁。
        if ((flags & 0x40000000u) && !(flags & (0x80000000u | 0x02000000u)) && size &&
            rva < module.size && size <= module.size - rva)
            result.emplace_back(module.base + rva, size);
    }
    std::sort(all.begin(), all.end());
    for (std::size_t i = 1; i < all.size(); ++i)
        if (all[i - 1].first + all[i - 1].second > all[i].first)
            return {};
    return result;
}
bool within(std::span<const StaticData::Range> regions, memory::Address address, std::size_t size) {
    for (const auto& [base, extent] : regions)
        if (address >= base && address - base < extent && size <= extent - (address - base))
            return true;
    return false;
}
} // namespace
void StaticData::initialize(memory::IReadChannel& reader, std::span<const memory::Module> modules,
                             std::span<const Range> preload) {
    auto image = std::make_shared<ImageData>();
    for (const auto& module : modules) {
        auto regions = read_regions(reader, module);
        image->regions.insert(image->regions.end(), regions.begin(), regions.end());
    }
    memory::ReadPlan plan(preload.size());
    std::vector<std::pair<Range, memory::ReadPlan::Ticket>> pending;
    for (const auto& range : preload) {
        if (!within(image->regions, range.first, range.second) || image->preloaded.contains(range))
            continue;
        auto& bytes = image->preloaded[range];
        bytes.resize(range.second);
        pending.emplace_back(range, plan.add(range.first, bytes.data(), bytes.size()));
    }
    plan.execute(reader);
    for (const auto& [range, ticket] : pending)
        if (!plan.complete(ticket))
            image->preloaded.erase(range); // 临时缺页不留下永久失败记录。
    records_.clear();
    image_ = std::move(image);
}
bool StaticData::contains(memory::Address address, std::size_t size) const noexcept {
    return image_ && size && within(image_->regions, address, size);
}
bool StaticData::copy(memory::IReadChannel& reader, memory::Address address, void* target, std::size_t size) const {
    const Range key{address, size};
    const std::vector<std::byte>* bytes = nullptr;
    if (const auto it = records_.find(key); it != records_.end())
        bytes = &it->second;
    else if (image_) {
        if (const auto initial = image_->preloaded.find(key); initial != image_->preloaded.end())
            bytes = &initial->second;
    }
    if (bytes) {
        std::memcpy(target, bytes->data(), size);
        return true;
    }
    if (!reader.read(address, target, size).complete(size))
        return false;
    if (contains(address, size)) {
        // 完整构造后才提交；分配失败不能留下可被后续任务命中的空记录。
        const auto* begin = static_cast<const std::byte*>(target);
        records_.emplace(key, std::vector<std::byte>(begin, begin + size));
    }
    return true;
}
std::optional<std::string> StaticData::text(memory::IReadChannel& reader, memory::Address address,
                                           std::size_t capacity) const {
    std::vector<char> bytes(capacity);
    if (!capacity || !memory::user_range(address, capacity) || !copy(reader, address, bytes.data(), bytes.size()))
        return {};
    const auto end = std::find(bytes.begin(), bytes.end(), '\0');
    return end == bytes.end() ? std::nullopt : std::optional(std::string(bytes.begin(), end));
}
} // namespace cs2::game
