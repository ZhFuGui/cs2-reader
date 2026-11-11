#pragma once
#include "memory/session.hpp"
namespace cs2::backends::hyperv::native {
struct KernelLayout {
    std::uint64_t process_head = 0;
    std::uint32_t pid = 0, links = 0, cr3 = 0, peb = 0, created = 0;
};
struct ProcessBinding {
    std::uint64_t cr3 = 0, peb = 0;
};
KernelLayout resolve_kernel_layout(std::uint64_t caller_cr3);
ProcessBinding resolve_process(const KernelLayout&, memory::ProcessIdentity, std::uint64_t caller_cr3);
} // namespace cs2::backends::hyperv::native
