#pragma once
#include <cstddef>
#include <cstdint>
namespace cs2::game::layout {
// Source 2 非 schema 布局。来自旧项目读取链；每次观察仍核验对象、identity 与完整句柄。
inline constexpr std::size_t chunk_table = 0x10, identities_per_chunk = 512, identity_stride = 0x70;
inline constexpr std::uint32_t entity_index_mask = 0x7FFF;
inline constexpr std::size_t identity_instance = 0, identity_handle = 0x10;
// 非反射骨骼布局：只有地址、模型与数组来源核验成功时才提供；实机验证单独报告。
inline constexpr std::size_t model_bone_array = 0x80, bone_stride = 0x20;
} // namespace cs2::game::layout
