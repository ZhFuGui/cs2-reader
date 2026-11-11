#include "memory/block_reader.hpp"
#include <limits>
#include <stdexcept>

namespace cs2::memory {
void Block::read(IReadChannel& channel, Address address, std::size_t bytes) {
    data_.resize(bytes);
    valid_.assign(bytes, 0);
    if (!bytes || !user_range(address, bytes))
        return;
    if (channel.read(address, data_.data(), bytes).complete(bytes)) {
        std::fill(valid_.begin(), valid_.end(), static_cast<unsigned char>(1));
        return;
    }
    std::vector<ReadRequest> pages;
    for (std::size_t offset = 0; offset < bytes;) {
        const auto count = std::min(bytes - offset, std::size_t(4096 - ((address + offset) & 4095)));
        pages.push_back({address + offset, data_.data() + offset, count});
        offset += count;
    }
    channel.read_many(pages);
    for (const auto& page : pages)
        if (page.result.complete(page.bytes))
            std::fill_n(valid_.begin() + static_cast<std::size_t>(page.address - address), page.bytes,
                        static_cast<unsigned char>(1));
}
bool Block::valid(std::size_t offset, std::size_t bytes) const noexcept {
    return offset <= valid_.size() && bytes <= valid_.size() - offset &&
           std::all_of(valid_.begin() + offset, valid_.begin() + offset + bytes, [](auto v) { return v != 0; });
}
std::optional<std::string> Block::text(std::size_t offset, std::size_t capacity) const {
    std::string result;
    for (std::size_t i = 0; i < capacity; ++i) {
        const auto c = get<char>(offset + i);
        if (!c)
            return std::nullopt;
        if (!*c)
            return result;
        result.push_back(*c);
    }
    return std::nullopt; // 没有终止符不能把截断字符串冒充完整文本。
}
namespace {
// 只决定读取分组，不限制字段覆盖。页判断使用实际地址，不能只看结构体偏移。
bool merge_fields(Address object, std::size_t begin, std::size_t end, const Field& next) noexcept {
    constexpr std::size_t max_span = 2048;
    constexpr std::size_t max_gap = 128;
    constexpr bool same_page_merge = true;
    const auto merged_end = std::max(end, next.offset + next.size);
    if (merged_end - begin > max_span)
        return false;
    const bool adjacent = next.offset <= end || next.offset - end <= max_gap;
    const bool same_page = ((object + begin) >> 12) == ((object + merged_end - 1) >> 12);
    return adjacent || (same_page_merge && same_page);
}
} // namespace
Fields::Fields(IReadChannel& channel, Address object, std::span<const Field> fields,
               std::span<const Field> additional) {
    if (fields.size() > values_.max_size() || additional.size() > values_.max_size() - fields.size())
        throw std::length_error("Field count overflow");
    values_.reserve(fields.size() + additional.size());
    const auto append = [&](std::span<const Field> source) {
        for (auto f : source) {
            if (!f.size || f.offset > std::numeric_limits<std::size_t>::max() - f.size ||
                object > std::numeric_limits<Address>::max() - f.offset || !user_range(object + f.offset, f.size))
                throw std::runtime_error("Invalid field range");
            values_.push_back({f.offset, f.size});
        }
    };
    append(fields);
    append(additional);
    std::sort(values_.begin(), values_.end(), [](const auto& a, const auto& b) { return a.offset < b.offset; });
    struct Cluster {
        std::size_t first, last, begin, offset, size;
    };
    std::vector<Cluster> clusters;
    std::size_t total = 0;
    for (std::size_t first = 0; first < values_.size();) {
        auto end = values_[first].offset + values_[first].size;
        std::size_t last = first + 1;
        while (last < values_.size() &&
               merge_fields(object, values_[first].offset, end, {values_[last].offset, values_[last].size})) {
            end = std::max(end, values_[last].offset + values_[last].size);
            ++last;
        }
        const auto bytes = end - values_[first].offset;
        if (bytes > std::numeric_limits<std::size_t>::max() - total)
            throw std::length_error("Field cluster buffer overflow");
        clusters.push_back({first, last, values_[first].offset, total, bytes});
        for (auto i = first; i < last; ++i)
            values_[i].storage = total + values_[i].offset - values_[first].offset;
        total += bytes;
        first = last;
    }
    data_.resize(total);
    std::vector<ReadRequest> requests;
    requests.reserve(clusters.size());
    for (const auto& cluster : clusters)
        requests.push_back({object + cluster.begin, data_.data() + cluster.offset, cluster.size});
    channel.read_many(requests);
    std::vector<std::size_t> fallback_indices;
    auto fallback_end = total;
    for (std::size_t cluster_index = 0; cluster_index < clusters.size(); ++cluster_index) {
        const auto& cluster = clusters[cluster_index];
        const bool complete = requests[cluster_index].result.complete(cluster.size);
        for (auto i = cluster.first; i < cluster.last; ++i) {
            auto& value = values_[i];
            if (complete) {
                value.valid = true;
            } else if (cluster.last - cluster.first > 1 &&
                       requests[cluster_index].result.status != TransferStatus::BackendFailure) {
                if (value.size > std::numeric_limits<std::size_t>::max() - fallback_end)
                    throw std::length_error("Field fallback buffer overflow");
                // 失败回退使用不重叠的独立区域，包含重叠或重复字段时也满足批量 ABI。
                value.storage = fallback_end;
                fallback_end += value.size;
                fallback_indices.push_back(i);
            }
        }
    }
    if (!fallback_indices.empty()) {
        // 仅失败路径扩展缓冲；首次请求已经返回，随后重建全部目标指针。
        data_.resize(fallback_end);
        requests.clear();
        requests.reserve(fallback_indices.size());
        for (auto i : fallback_indices) {
            const auto& value = values_[i];
            requests.push_back({object + value.offset, data_.data() + value.storage, value.size});
        }
        channel.read_many(requests);
        for (std::size_t i = 0; i < requests.size(); ++i)
            values_[fallback_indices[i]].valid = requests[i].result.complete(requests[i].bytes);
    }
}
std::optional<std::string> Fields::text(std::size_t offset) const {
    for (const auto& value : values_) {
        if (value.offset != offset || !value.valid)
            continue;
        const auto* start = reinterpret_cast<const char*>(data_.data() + value.storage);
        const auto* end = static_cast<const char*>(std::memchr(start, 0, value.size));
        if (end)
            return std::string(start, end);
    }
    return std::nullopt;
}
std::optional<std::string> read_string(IReadChannel& channel, Address address, std::size_t capacity) {
    if (!user_range(address, capacity))
        return std::nullopt;
    Block block;
    block.read(channel, address, capacity);
    return block.text(0, capacity);
}
} // namespace cs2::memory
