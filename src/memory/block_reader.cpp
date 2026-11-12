#include "memory/block_reader.hpp"
#include "memory/read_plan.hpp"
#include <limits>
#include <stdexcept>

namespace cs2::memory {
void Block::read(IReadChannel& channel, Address address, std::size_t bytes) {
    const Read request{*this, address, bytes};
    read_many(channel, std::span(&request, 1));
}
void Block::read_many(IReadChannel& channel, std::span<const Read> blocks) {
    struct Item {
        const Read* source;
        ReadPlan::Ticket ticket;
    };
    ReadPlan opening(blocks.size());
    std::vector<Item> items;
    for (const auto& request : blocks) {
        auto& block = request.block;
        block.data_.resize(request.bytes);
        block.valid_.assign(request.bytes, 0);
        if (request.bytes && user_range(request.address, request.bytes))
            items.push_back({&request, opening.add(request.address, block.data_.data(), request.bytes)});
    }
    opening.execute(channel);
    struct Page {
        Block* block;
        std::size_t offset, bytes;
        ReadPlan::Ticket ticket;
    };
    ReadPlan fallback;
    std::vector<Page> pages;
    for (const auto& item : items) {
        const auto& request = *item.source;
        auto& block = request.block;
        if (opening.complete(item.ticket)) {
            std::fill(block.valid_.begin(), block.valid_.end(), static_cast<unsigned char>(1));
            continue;
        }
        for (std::size_t offset = 0; offset < request.bytes;) {
            const auto count =
                std::min(request.bytes - offset, std::size_t(4096 - ((request.address + offset) & 4095)));
            pages.push_back(
                {&block, offset, count, fallback.add(request.address + offset, block.data_.data() + offset, count)});
            offset += count;
        }
    }
    fallback.execute(channel);
    for (const auto& page : pages)
        if (fallback.complete(page.ticket))
            std::fill_n(page.block->valid_.begin() + page.offset, page.bytes, static_cast<unsigned char>(1));
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
Fields::Fields(IReadChannel& channel, Address object, std::span<const Field> fields, std::span<const Field> additional)
    : Fields(object, fields, additional) {
    Fields* self = this;
    read_many(channel, std::span(&self, 1));
}
Fields::Fields(Address object, std::span<const Field> fields, std::span<const Field> additional) : object_(object) {
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
        clusters_.push_back({first, last, values_[first].offset, total, bytes});
        for (auto i = first; i < last; ++i)
            values_[i].storage = total + values_[i].offset - values_[first].offset;
        total += bytes;
        first = last;
    }
    data_.resize(total);
}
void Fields::read_many(IReadChannel& channel, std::span<Fields* const> objects) {
    ReadPlan opening;
    std::vector<ReadPlan::Ticket> tickets;
    for (auto* fields : objects) {
        if (!fields->clusters_.empty()) {
            const auto& last = fields->clusters_.back();
            fields->data_.resize(last.offset + last.size);
        }
        for (auto& value : fields->values_)
            value.valid = false;
        for (const auto& cluster : fields->clusters_) {
            for (auto i = cluster.first; i < cluster.last; ++i)
                fields->values_[i].storage = cluster.offset + fields->values_[i].offset - cluster.begin;
            tickets.push_back(
                opening.add(fields->object_ + cluster.begin, fields->data_.data() + cluster.offset, cluster.size));
        }
    }
    opening.execute(channel);
    struct Retry {
        Fields* fields;
        std::size_t index;
        ReadPlan::Ticket ticket;
    };
    ReadPlan fallback;
    std::vector<Retry> retries;
    std::size_t cursor = 0;
    for (auto* fields : objects) {
        std::vector<std::size_t> indices;
        auto fallback_end = fields->data_.size();
        for (const auto& cluster : fields->clusters_) {
            const auto ticket = tickets[cursor++];
            const bool complete = opening.complete(ticket);
            for (auto i = cluster.first; i < cluster.last; ++i) {
                auto& value = fields->values_[i];
                if (complete) {
                    value.valid = true;
                } else if (cluster.last - cluster.first > 1 &&
                           opening.result(ticket).status != TransferStatus::BackendFailure) {
                    if (value.size > std::numeric_limits<std::size_t>::max() - fallback_end)
                        throw std::length_error("Field fallback buffer overflow");
                    // 失败回退使用不重叠的独立区域，包含重叠或重复字段时也满足批量 ABI。
                    value.storage = fallback_end;
                    fallback_end += value.size;
                    indices.push_back(i);
                }
            }
        }
        if (!indices.empty()) {
            // 仅失败路径扩展缓冲；首次请求已经返回，随后重建全部目标指针。
            fields->data_.resize(fallback_end);
            for (auto i : indices) {
                const auto& value = fields->values_[i];
                retries.push_back(
                    {fields, i,
                     fallback.add(fields->object_ + value.offset, fields->data_.data() + value.storage, value.size)});
            }
        }
    }
    fallback.execute(channel);
    for (const auto& retry : retries)
        retry.fields->values_[retry.index].valid = fallback.complete(retry.ticket);
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
