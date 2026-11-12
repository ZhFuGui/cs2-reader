#pragma once
#include "memory/read_channel.hpp"
#include "model/skeleton.hpp"
namespace cs2::game {
struct SkeletonRead {
    memory::Address pawn;
    std::optional<memory::Address> scene;
    model::Skeleton& result;
};
void sample_skeletons(memory::IReadChannel&, std::span<const SkeletonRead>);
// scene 来自本轮人物开读；所属关系和关闭核验仍由骨骼采样独立执行。
model::Skeleton sample_skeleton(memory::IReadChannel&, memory::Address pawn, std::optional<memory::Address> scene);
} // namespace cs2::game
