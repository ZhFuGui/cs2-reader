#pragma once
#include "backends/hyperv/native/abi.hpp"
#include <cstddef>
#include <cstdint>

namespace cs2::backends::hyperv::native {
struct ReadCounters {
    std::uint64_t single = 0, batch = 0, items = 0, errors = 0;
    std::uint64_t item_splits = 0, byte_splits = 0;
};
// 握手与本进程自读成功后返回调用方 CR3；不可用或协议不匹配返回 0。
std::uint64_t probe_address_space() noexcept;
bool probe_list_find(std::uint64_t caller_cr3) noexcept;
// true 仅代表完整结果有效；必须分别处理 Found/More/失败状态。
// 请求和结果必须独占且不重叠；不保留指针，不提供原子快照或对象引用。
bool find_list(std::uint64_t source_cr3, const abi::ListFindRequest&, abi::ListFindResult&) noexcept;
// caller_cr3 来自当前绑定的成功探针，避免每次读取都额外查询 CR3。
// 上游只返回 bool：只有明确成功才返回 bytes，失败不推测已复制的前缀长度。
std::size_t read(std::uint64_t caller_cr3, std::uint64_t source_cr3, std::uint64_t address, void* output,
                 std::size_t bytes, ReadCounters* counters = nullptr) noexcept;
// 失败可能已写入部分目标；返回 0 不代表没有副作用，禁止自动重试。
std::size_t write(std::uint64_t caller_cr3, std::uint64_t destination_cr3, std::uint64_t address, const void* input,
                  std::size_t bytes) noexcept;
} // namespace cs2::backends::hyperv::native
