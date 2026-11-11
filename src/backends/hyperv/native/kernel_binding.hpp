#pragma once
#include "memory/session.hpp"
#include <optional>
#include <string_view>
namespace cs2::backends::hyperv::native {
struct KernelLayout {
    std::uint64_t process_head = 0;
    std::uint32_t pid = 0, links = 0, cr3 = 0, peb = 0, created = 0;
    std::uint32_t exit_flags = 0, exit_mask = 0, image_name = 0;
};
struct ProcessBinding {
    std::uint64_t cr3 = 0, peb = 0, object = 0;
};
struct ProcessLookupStats {
    std::uint32_t calls = 0, nodes = 0, restarts = 0;
};
KernelLayout resolve_kernel_layout(std::uint64_t caller_cr3);
ProcessBinding resolve_process(const KernelLayout&, memory::ProcessIdentity, std::uint64_t caller_cr3,
                               ProcessLookupStats* statistics = nullptr);
std::optional<memory::ProcessIdentity> discover_process(const KernelLayout&, std::string_view image_name,
                                                        std::uint64_t caller_cr3);
memory::ProcessState observe_process(const KernelLayout&, memory::ProcessIdentity, const ProcessBinding&,
                                     std::uint64_t caller_cr3);
} // namespace cs2::backends::hyperv::native
