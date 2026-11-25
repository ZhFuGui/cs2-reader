#pragma once
#include "game/cs2/session/context.hpp"
#include <string_view>
#include <vector>
namespace cs2::game {
struct CvarRequest {
    std::size_t root;
    bool real;
    std::string_view name{};
};
// 一次开读和一次关闭；每个配置独立成功，不缓存跨批值，不解释分槽业务规则。
class CvarBatch {
  public:
    struct Sample {
        memory::Address root{}, data{}, name{};
        std::uint16_t type{};
        std::uint32_t flags{}, bits{};
        bool readable = false;
        std::optional<float> real() const;
        std::optional<std::uint8_t> boolean() const;
    };
    CvarBatch(memory::IReadChannel&, const Context&, std::span<const CvarRequest>);
    const Sample& at(std::size_t i) const { return samples_.at(i); }
    std::vector<std::uint8_t> close(memory::IReadChannel&) const;
  private:
    std::vector<CvarRequest> requests_;
    std::vector<Sample> samples_;
    std::vector<std::string> names_;
};
} // namespace cs2::game
