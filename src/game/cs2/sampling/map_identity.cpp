#include "game/cs2/sampling/map_identity.hpp"
#include "game/cs2/offsets/catalog.hpp"
#include "game/cs2/world/profile.hpp"
#include "memory/block_reader.hpp"
#include <array>
#include <limits>
#include <stdexcept>

namespace cs2::game {
MapObservation MapIdentitySampler::begin(memory::IReadChannel& reader, const Context& context) const {
    MapObservation result;
    if (context.build != world_profile::build) {
        result.status = model::MapStatus::Unsupported;
        return result;
    }
    result.status = model::MapStatus::SourceUnavailable;
    const auto root = memory::read_value<memory::Address>(reader, context.engine.base + offsets::engine::dwNetworkGameClient);
    if (!root)
        return result;
    result.network = *root;
    if (!*root) {
        result.status = model::MapStatus::NotInLevel;
        return result;
    }
    if (!memory::user_range(*root, offsets::engine::dwNetworkGameClient_isBackgroundMap + 1))
        return result;
    // 原生路径、名称与 sign-on 状态相邻，一次读取 36 字节，不复制中间的大型网络对象。
    std::array<std::byte, world_profile::map_header_bytes> header{};
    if (!reader.read(*root + world_profile::map_header, header.data(), header.size()).complete(header.size()))
        return result;
    std::memcpy(&result.path_pointer, header.data(), sizeof(result.path_pointer));
    std::memcpy(&result.name_pointer, header.data() + world_profile::name_in_header, sizeof(result.name_pointer));
    std::memcpy(&result.sign_on, header.data() + world_profile::sign_on_in_header, sizeof(result.sign_on));
    const auto background = memory::read_value<std::uint8_t>(reader, *root + offsets::engine::dwNetworkGameClient_isBackgroundMap);
    if (!background || *background > 1)
        return result;
    result.background = *background;
    // 与原生 IsInGame 条件一致；观战不依赖 Local Pawn 是否仍有身体。
    if (result.background || result.sign_on < world_profile::full_sign_on) {
        result.status = model::MapStatus::NotInLevel;
        return result;
    }
    if (!memory::user_range(result.name_pointer) || !memory::user_range(result.path_pointer))
        return result;
    const auto name = memory::read_string(reader, result.name_pointer, world_profile::maximum_text_bytes);
    const auto path = memory::read_string(reader, result.path_pointer, world_profile::maximum_text_bytes);
    if (!name || name->empty() || !path || path->empty())
        return result;
    result.name = *name;
    result.resource_path = *path;
    result.status = model::MapStatus::Available;
    return result;
}

model::MapIdentity MapIdentitySampler::finish(memory::IReadChannel& reader, const Context& context,
                                            const MapObservation& opening) {
    auto closing = begin(reader, context);
    // 切图横跨本次采集时，不能把最后看到的新地图贴到先前采样的实体上。
    if (opening != closing) {
        closing.status = model::MapStatus::Transition;
        closing.name.reset();
        closing.resource_path.reset();
    }
    if (!previous_ || *previous_ != closing) {
        if (generation_ == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("Map identity generation exhausted");
        ++generation_;
        previous_ = closing;
    }
    return {closing.status, generation_, std::move(closing.name), std::move(closing.resource_path)};
}
} // namespace cs2::game
