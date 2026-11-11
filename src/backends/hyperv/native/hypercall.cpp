#include "backends/hyperv/native/hypercall.hpp"
#include "backends/hyperv/native/abi.hpp"
#include <array>
#include <cstring>
#include <limits>

extern "C" std::uint64_t cs2_hypercall(const cs2::backends::hyperv::native::abi::Request* request);
namespace cs2::backends::hyperv::native {
namespace {
std::uint64_t invoke(const abi::Request& request) noexcept {
    return cs2_hypercall(&request);
}
bool valid_cr3(std::uint64_t cr3) noexcept {
    // PCID 等低位按原值传递；页表基址必须非零。
    return (cr3 & 0x000FFFFFFFFFF000ULL) != 0;
}
bool range(std::uint64_t address, std::size_t bytes) noexcept {
    if (bytes && address > std::numeric_limits<std::uint64_t>::max() - (bytes - 1))
        return false;
    auto canonical = [](std::uint64_t value) {
        const auto top = value >> 47;
        return top == 0 || top == 0x1FFFF;
    };
    const auto end = address + (bytes ? bytes - 1 : 0);
    return canonical(address) && canonical(end) && (address >> 47) == (end >> 47);
}
void prepare(void* destination, std::size_t bytes) noexcept {
    if (!bytes)
        return;
    auto* output = static_cast<volatile unsigned char*>(destination);
    output[0] = 0;
    for (std::size_t offset = 4096 - (reinterpret_cast<std::uintptr_t>(destination) & 4095); offset < bytes;
         offset += 4096)
        output[offset] = 0;
}
void prepare(const void* source, std::size_t bytes) noexcept {
    if (!bytes)
        return;
    const auto* input = static_cast<const volatile unsigned char*>(source);
    (void)input[0];
    for (std::size_t offset = 4096 - (reinterpret_cast<std::uintptr_t>(source) & 4095); offset < bytes; offset += 4096)
        (void)input[offset];
}
bool link_address(std::uint64_t address) noexcept {
    return address && !(address & 7) && range(address, 16);
}
} // namespace
bool find_list(std::uint64_t source_cr3, const abi::ListFindRequest& query, abi::ListFindResult& result) noexcept {
    const auto input = reinterpret_cast<std::uint64_t>(&query);
    const auto output = reinterpret_cast<std::uint64_t>(&result);
    // 重叠时不能先清空输出，否则会破坏请求。
    if (!range(input, sizeof(query)) || !range(output, sizeof(result)) ||
        (input <= output ? output - input < sizeof(query) : input - output < sizeof(result)))
        return false;
    result = {};
    const auto end = static_cast<std::uint64_t>(query.window_offset) + query.window_size;
    if (!(source_cr3 & 0x0000FFFFFFFFF000ULL) || (source_cr3 >> 48) ||
        query.version != abi::list_version || query.struct_size != sizeof(query) ||
        !link_address(query.head_va) || !query.window_size || query.window_size > abi::list_max_window ||
        !query.key_size || query.key_size > abi::list_max_key || query.node_budget > abi::list_max_nodes ||
        query.window_offset > query.link_offset || query.window_offset > query.key_offset ||
        end < static_cast<std::uint64_t>(query.link_offset) + 16 ||
        end < static_cast<std::uint64_t>(query.key_offset) + query.key_size ||
        (!query.resume_link && query.expected_prev) ||
        (query.resume_link && (!link_address(query.resume_link) || !link_address(query.expected_prev) ||
                              query.resume_link == query.head_va || query.resume_link == query.expected_prev)))
        return false;
    prepare(static_cast<const void*>(&query), sizeof(query));
    prepare(&result, sizeof(result));
    const auto call = abi::request(abi::Operation::FindList, source_cr3, input, output);
    if (invoke(call) != 1) {
        result = {};
        return false;
    }
    const auto budget = query.node_budget ? query.node_budget : abi::list_default_nodes;
    bool valid = result.visited <= budget && result.reserved == 0;
    switch (result.status) {
    case abi::ListStatus::Found:
        valid = valid && result.visited && result.object_va && range(result.object_va, 1) &&
                query.window_offset <= UINT64_MAX - result.object_va &&
                range(result.object_va + query.window_offset, query.window_size) &&
                result.window_size == query.window_size && !result.next_link && !result.previous_link &&
                std::memcmp(result.window + query.key_offset - query.window_offset, query.key, query.key_size) == 0;
        break;
    case abi::ListStatus::More:
        valid = valid && result.visited == budget && !result.object_va && !result.window_size &&
                link_address(result.next_link) && link_address(result.previous_link) &&
                result.next_link != result.previous_link && result.next_link != query.head_va;
        break;
    case abi::ListStatus::NotFound:
    case abi::ListStatus::Unreadable:
    case abi::ListStatus::Inconsistent:
        valid = valid && !result.object_va && !result.window_size && !result.next_link && !result.previous_link;
        break;
    default:
        valid = false;
    }
    if (!valid) result = {};
    return valid;
}
bool probe_list_find(std::uint64_t caller_cr3) noexcept {
    struct Node { std::uint64_t next, previous, key; };
    std::array<std::uint64_t, 2> head{};
    std::array<Node, 2> nodes{};
    const auto head_va = reinterpret_cast<std::uint64_t>(head.data());
    const auto first = reinterpret_cast<std::uint64_t>(&nodes[0]);
    const auto second = reinterpret_cast<std::uint64_t>(&nodes[1]);
    head = {first, second};
    nodes[0] = {second, head_va, 0x14DAB87C923E610FULL};
    nodes[1] = {head_va, first, 0x67C2E91A4B503DF8ULL};
    abi::ListFindRequest query{};
    query.version = abi::list_version;
    query.struct_size = sizeof(query);
    query.head_va = head_va;
    query.window_size = sizeof(Node);
    query.key_offset = offsetof(Node, key);
    query.key_size = sizeof(nodes[1].key);
    std::memcpy(query.key, &nodes[1].key, sizeof(nodes[1].key));
    abi::ListFindResult result{};
    return find_list(caller_cr3, query, result) && result.status == abi::ListStatus::Found &&
           result.visited == 2 && result.object_va == second &&
           std::memcmp(result.window, &nodes[1], sizeof(Node)) == 0;
}
std::size_t read(std::uint64_t caller_cr3, std::uint64_t source_cr3, std::uint64_t address, void* output,
                 std::size_t bytes, ReadCounters* counters) noexcept {
    if ((!output && bytes) || !range(address, bytes) || !range(reinterpret_cast<std::uint64_t>(output), bytes) ||
        !valid_cr3(caller_cr3) || !valid_cr3(source_cr3))
        return 0;
    if (!bytes)
        return 0;
    prepare(output, bytes);
    if (counters)
        ++counters->single;
    const auto copied = invoke(abi::request(abi::Operation::VirtualCopy, source_cr3, address, caller_cr3,
                                            reinterpret_cast<std::uint64_t>(output), bytes));
    if (copied == 1)
        return bytes;
    // 上游失败前可能已复制一部分；整段失效并清理本地缓冲，不调用旧物理读取回退。
    std::memset(output, 0, bytes);
    return 0;
}
std::size_t write(std::uint64_t caller_cr3, std::uint64_t destination_cr3, std::uint64_t address, const void* input,
                  std::size_t bytes) noexcept {
    if ((!input && bytes) || !range(address, bytes) || !range(reinterpret_cast<std::uint64_t>(input), bytes) ||
        !valid_cr3(caller_cr3) || !valid_cr3(destination_cr3))
        return 0;
    if (!bytes)
        return 0;
    prepare(input, bytes); // 仅触及 Reader 自己的源缓冲，不主动触发目标进程缺页。
    const auto copied = invoke(abi::request(abi::Operation::VirtualCopy, caller_cr3,
                                            reinterpret_cast<std::uint64_t>(input), destination_cr3, address, bytes));
    // 部分写入不自动重试，避免隐藏已经发生的副作用。
    return copied == 1 ? bytes : 0;
}
std::uint64_t probe_address_space() noexcept {
    if (invoke(abi::request(abi::Operation::Test)) != abi::test_response)
        return 0;
    const auto cr3 = invoke(abi::request(abi::Operation::CurrentCr3));
    if (!valid_cr3(cr3))
        return 0;
    const std::array<std::uint64_t, 2> expected{0xBE0762D8A4F1935CULL, 0x51C3B980DE2A746FULL};
    std::array<std::uint64_t, 2> observed{};
    if (read(cr3, cr3, reinterpret_cast<std::uint64_t>(expected.data()), observed.data(), sizeof(observed)) !=
            sizeof(observed) ||
        observed != expected)
        return 0;
    return cr3;
}
} // namespace cs2::backends::hyperv::native
