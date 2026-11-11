#include "backends/hyperv/native/kernel_locator.hpp"
#include "backends/hyperv/native/abi.hpp"
#include "backends/hyperv/native/batch.hpp"
#include "backends/hyperv/native/hypercall.hpp"
#include "backends/hyperv/native/kernel_image.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

extern "C" std::uint64_t cs2_hypercall(const cs2::backends::hyperv::native::abi::Request* request);
namespace cs2::backends::hyperv::native {
namespace {
using Table = std::array<std::uint64_t, 512>;
constexpr std::uint64_t physical_page_mask = 0x000FFFFFFFFFF000ULL;
constexpr std::uint64_t kernel_min = 0xFFFF800000000000ULL;
constexpr std::uint64_t region_bytes = 0x200000;
constexpr std::uint64_t max_image_bytes = 0x10000000; // 与 KernelImage 的映像大小上限一致。
constexpr std::uint64_t nx_bit = 1ULL << 63;
constexpr std::size_t max_table_reads = 100000;
constexpr std::size_t max_executable_regions = 16384;
constexpr std::size_t max_candidate_pages = 1000000;

std::uint64_t invoke(abi::Operation operation, std::uint64_t a0, std::uint64_t a1,
                     std::uint64_t a2 = 0) noexcept {
    const auto call = abi::request(operation, a0, a1, a2);
    return cs2_hypercall(&call);
}

class PhysicalTableReader {
  public:
    explicit PhysicalTableReader(std::uint64_t caller_cr3) {
        page_ = VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!page_)
            throw std::runtime_error("Cannot allocate page-table receive buffer");
        if (!VirtualLock(page_, 4096)) {
            VirtualFree(page_, 0, MEM_RELEASE);
            throw std::runtime_error("Cannot pin page-table receive buffer");
        }
        *static_cast<volatile std::uint8_t*>(page_) = 0;
        destination_gpa_ = invoke(abi::Operation::TranslateVa, caller_cr3,
                                  reinterpret_cast<std::uint64_t>(page_));
        if (!destination_gpa_ || (destination_gpa_ & 4095)) {
            VirtualUnlock(page_, 4096);
            VirtualFree(page_, 0, MEM_RELEASE);
            throw std::runtime_error("Cannot translate page-table receive buffer");
        }
    }
    ~PhysicalTableReader() {
        VirtualUnlock(page_, 4096);
        VirtualFree(page_, 0, MEM_RELEASE);
    }
    PhysicalTableReader(const PhysicalTableReader&) = delete;
    PhysicalTableReader& operator=(const PhysicalTableReader&) = delete;

    [[nodiscard]] Table table(std::uint64_t source_gpa) {
        if (!source_gpa || (source_gpa & 4095) || ++reads_ > max_table_reads)
            throw std::runtime_error("Invalid or excessive kernel page-table traversal");
        std::memset(page_, 0, 4096);
        // 目标是锁定的 Reader 缓冲页；不会写目标进程或内核页面。
        if (invoke(abi::Operation::PhysicalCopy, source_gpa, destination_gpa_, 4096) != 1)
            throw std::runtime_error("Kernel page-table physical copy failed");
        Table result{};
        std::memcpy(result.data(), page_, 4096);
        return result;
    }
  private:
    void* page_ = nullptr;
    std::uint64_t destination_gpa_ = 0;
    std::size_t reads_ = 0;
};

std::vector<std::uint64_t> executable_regions(PhysicalTableReader& reader, const Table& pml4) {
    std::vector<std::uint64_t> result;
    for (unsigned i = 256; i < 512; ++i) {
        const auto pml4e = pml4[i];
        if (!(pml4e & 1) || (pml4e & nx_bit)) continue;
        const auto pdpt = reader.table(pml4e & physical_page_mask);
        for (unsigned j = 0; j < 512; ++j) {
            const auto pdpte = pdpt[j];
            if (!(pdpte & 1) || (pdpte & nx_bit)) continue;
            if (pdpte & 0x80) {
                for (unsigned k = 0; k < 512; ++k) {
                    result.push_back(kernel_min | (std::uint64_t(i) << 39) |
                                     (std::uint64_t(j) << 30) | (std::uint64_t(k) << 21));
                    if (result.size() > max_executable_regions)
                        throw std::runtime_error("Too many executable kernel regions");
                }
                continue;
            }
            const auto pd = reader.table(pdpte & physical_page_mask);
            for (unsigned k = 0; k < 512; ++k) {
                const auto pde = pd[k];
                if (!(pde & 1) || (pde & nx_bit)) continue;
                bool executable = (pde & 0x80) != 0;
                if (!executable) {
                    const auto pt = reader.table(pde & physical_page_mask);
                    executable = std::any_of(pt.begin(), pt.end(), [](std::uint64_t entry) {
                        return (entry & 1) && !(entry & nx_bit);
                    });
                }
                if (!executable) continue;
                result.push_back(kernel_min | (std::uint64_t(i) << 39) |
                                 (std::uint64_t(j) << 30) | (std::uint64_t(k) << 21));
                if (result.size() > max_executable_regions)
                    throw std::runtime_error("Too many executable kernel regions");
            }
        }
    }
    if (result.empty())
        throw std::runtime_error("Current CR3 has no readable executable kernel mapping");
    return result;
}

std::vector<std::uint64_t> candidate_regions(const std::vector<std::uint64_t>& executable) {
    std::vector<std::uint64_t> result;
    result.reserve(executable.size() * (1 + max_image_bytes / region_bytes));
    for (const auto address : executable)
        for (std::uint64_t back = 0; back <= max_image_bytes / region_bytes; ++back)
            if (address >= kernel_min + back * region_bytes)
                result.push_back(address - back * region_bytes);
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::vector<std::uint64_t> mapped_pages(const std::vector<std::uint64_t>& regions,
                                         const Table& pml4, PhysicalTableReader& reader) {
    std::vector<std::uint64_t> result;
    for (const auto region : regions) {
        const auto i = static_cast<unsigned>((region >> 39) & 511);
        const auto j = static_cast<unsigned>((region >> 30) & 511);
        const auto k = static_cast<unsigned>((region >> 21) & 511);
        if (!(pml4[i] & 1)) continue;
        const auto pdpt = reader.table(pml4[i] & physical_page_mask);
        if (!(pdpt[j] & 1)) continue;
        bool all_present = (pdpt[j] & 0x80) != 0;
        Table pd{};
        if (!all_present) {
            pd = reader.table(pdpt[j] & physical_page_mask);
            if (!(pd[k] & 1)) continue;
            all_present = (pd[k] & 0x80) != 0;
        }
        Table pt{};
        if (!all_present)
            pt = reader.table(pd[k] & physical_page_mask);
        for (unsigned page = 0; page < 512; ++page)
            if (all_present || (pt[page] & 1))
                result.push_back(region + (std::uint64_t(page) << 12));
        if (result.size() > max_candidate_pages)
            throw std::runtime_error("Too many mapped kernel image candidates");
    }
    return result;
}

bool active_kernel_image(std::uint64_t cr3, std::uint64_t base) {
    try {
        const KernelImage image(cr3, base);
        const auto initial = image.export_address("PsInitialSystemProcess", false);
        const auto pid_offset = image.accessor_offset("PsGetProcessId");
        std::uint64_t system = 0, pid = 0;
        return read(cr3, cr3, initial, &system, sizeof(system)) == sizeof(system) &&
               system >= kernel_min && system <= UINT64_MAX - 0x1000 && !(system & 7) &&
               read(cr3, cr3, system + pid_offset, &pid, sizeof(pid)) == sizeof(pid) && pid == 4;
    } catch (const std::runtime_error&) {
        return false;
    }
}
} // namespace

std::uint64_t locate_kernel_base(std::uint64_t caller_cr3) {
    if (!(caller_cr3 & physical_page_mask))
        throw std::runtime_error("Current CR3 has no physical page-table root");
    PhysicalTableReader reader(caller_cr3);
    const auto pml4 = reader.table(caller_cr3 & physical_page_mask);
    const auto regions = candidate_regions(executable_regions(reader, pml4));
    const auto pages = mapped_pages(regions, pml4, reader);
    std::uint64_t kernel_base = 0;
    for (std::size_t begin = 0; begin < pages.size(); begin += abi::batch_max_items) {
        const auto count = std::min<std::size_t>(abi::batch_max_items, pages.size() - begin);
        std::array<abi::BatchItem, abi::batch_max_items> items{};
        std::array<std::uint16_t, abi::batch_max_items> magic{};
        for (std::size_t i = 0; i < count; ++i)
            items[i] = {pages[begin + i], reinterpret_cast<std::uint64_t>(&magic[i]), 2};
        alignas(8) std::uint64_t successful = 0;
        if (!read_batch(caller_cr3, std::span<const abi::BatchItem>{items.data(), count}, successful))
            throw std::runtime_error("Kernel image candidate batch read failed");
        for (std::size_t i = 0; i < count; ++i) {
            if (!(successful & (1ULL << i)) || magic[i] != IMAGE_DOS_SIGNATURE ||
                !active_kernel_image(caller_cr3, pages[begin + i]))
                continue;
            if (kernel_base)
                throw std::runtime_error("Multiple active NT kernel images found");
            kernel_base = pages[begin + i];
        }
    }
    if (!kernel_base)
        throw std::runtime_error("Active NT kernel image is absent from current CR3 mappings");
    return kernel_base;
}
} // namespace cs2::backends::hyperv::native
