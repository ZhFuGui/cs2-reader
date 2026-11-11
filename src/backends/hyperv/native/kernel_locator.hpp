#pragma once
#include <cstdint>

namespace cs2::backends::hyperv::native {
// 仅用现有内存超调用定位运行中的 NT 映像；不查询系统模块表。
[[nodiscard]] std::uint64_t locate_kernel_base(std::uint64_t caller_cr3);
}
