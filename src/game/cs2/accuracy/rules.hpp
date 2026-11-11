#pragma once
#include "game/cs2/session/context.hpp"
#include "memory/observation.hpp"
#include "model/accuracy.hpp"
#include <array>
#include <memory>
namespace cs2::game {
// 一次采集独占事务；构造后只读共享给worker，工作池结束后由协调器close。
// 没有跨批值缓存；失败按实际使用规则的位集撤销，不污染独立Spread。
class AccuracyRules {
  public:
    AccuracyRules(memory::IReadChannel&, const Context&);
    struct Value {
        std::optional<float> real;
        std::optional<std::uint8_t> boolean;
        model::AccuracyStatus status = model::AccuracyStatus::RuleUnavailable;
    };
    const Value& at(std::size_t root, std::uint32_t& used) const;
    std::uint32_t close();
  private:
    std::array<Value,7> values_;
    std::array<std::unique_ptr<memory::Observation>,7> proofs_;
};
void revoke_accuracy(model::WeaponAccuracyEvaluation&, model::AccuracyStatus);
} // namespace cs2::game
