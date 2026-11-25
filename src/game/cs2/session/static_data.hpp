#pragma once
#include "memory/observation.hpp"
#include "memory/modules.hpp"
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace cs2::game {
// 只保存绑定模块的稳定只读字节，不保存实体、资源或配置的动态值。
// 初始化记录共享只读；每个采样上下文独占惰性记录，不跨 worker 共享可写状态。
class StaticData {
  public:
    using Range = std::pair<memory::Address, std::size_t>;
    void initialize(memory::IReadChannel&, std::span<const memory::Module>, std::span<const Range> preload);
    [[nodiscard]] bool contains(memory::Address, std::size_t) const noexcept;
    template <class T>
    std::optional<T> value(memory::IReadChannel& reader, memory::Address address,
                           memory::Observation* observation = nullptr) const {
        static_assert(std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>);
        if (!contains(address, sizeof(T)))
            return observation ? observation->get<T>(address) : memory::read_value<T>(reader, address);
        T result{};
        return copy(reader, address, &result, sizeof(T)) ? std::optional(result) : std::nullopt;
    }
    std::optional<std::string> text(memory::IReadChannel&, memory::Address, std::size_t capacity) const;

  private:
    using Records = std::map<Range, std::vector<std::byte>>;
    struct ImageData {
        std::vector<Range> regions;
        Records preloaded;
    };
    std::shared_ptr<const ImageData> image_;
    mutable Records records_;
    bool copy(memory::IReadChannel&, memory::Address, void*, std::size_t) const;
};
} // namespace cs2::game
