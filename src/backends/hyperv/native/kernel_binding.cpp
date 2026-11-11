#include "backends/hyperv/native/kernel_binding.hpp"
#include "backends/hyperv/native/hypercall.hpp"
#include "backends/hyperv/native/kernel_image.hpp"
#include "diagnostics/obf.hpp"
#include "platform/windows/native_api.hpp"
#include "platform/windows/process_catalog.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <intrin.h>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" __declspec(dllimport) NTSTATUS NTAPI RtlAdjustPrivilege(ULONG, BOOLEAN, BOOLEAN, PBOOLEAN);
namespace cs2::backends::hyperv::native {
namespace {
constexpr std::uint64_t kernel_min = 0xFFFF800000000000ULL;
struct ModuleEntry {
    HANDLE section;
    void* mapped;
    void* base;
    ULONG size, flags;
    USHORT load, init, count, name_offset;
    unsigned char path[256];
};
struct Modules { ULONG count; ModuleEntry entries[1]; };

std::uint64_t kernel_base() {
    BOOLEAN previous = FALSE;
    const auto privilege = RtlAdjustPrivilege(20, TRUE, FALSE, &previous);
    if (privilege < 0)
        throw std::runtime_error("Kernel module query requires SeDebugPrivilege");
    struct Restore {
        BOOLEAN previous;
        ~Restore() {
            BOOLEAN ignored = FALSE;
            RtlAdjustPrivilege(20, previous, FALSE, &ignored);
        }
    } restore{previous};
    ULONG required = 0;
    ::NtQuerySystemInformation(static_cast<SYSTEM_INFORMATION_CLASS>(11), nullptr, 0, &required);
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        if (required < offsetof(Modules, entries) || required > 16 * 1024 * 1024)
            throw std::runtime_error("Invalid kernel module query size");
        std::vector<std::byte> buffer(static_cast<std::size_t>(required) + 16384);
        const auto status = ::NtQuerySystemInformation(static_cast<SYSTEM_INFORMATION_CLASS>(11), buffer.data(),
                                                       static_cast<ULONG>(buffer.size()), &required);
        if (status == platform::windows::status_length_mismatch)
            continue;
        if (status < 0 || required > buffer.size() || required < offsetof(Modules, entries))
            throw std::runtime_error("Kernel module query failed");
        ULONG count = 0;
        std::memcpy(&count, buffer.data(), sizeof(count));
        if (count > (required - offsetof(Modules, entries)) / sizeof(ModuleEntry))
            throw std::runtime_error("Invalid kernel module entries");
        for (ULONG i = 0; i < count; ++i) {
            ModuleEntry entry{};
            std::memcpy(&entry, buffer.data() + offsetof(Modules, entries) + i * sizeof(entry), sizeof(entry));
            if (entry.name_offset >= sizeof(entry.path) ||
                !std::memchr(entry.path + entry.name_offset, 0, sizeof(entry.path) - entry.name_offset))
                continue;
            const auto* name = reinterpret_cast<const char*>(entry.path + entry.name_offset);
            if (_stricmp(name, OBF("ntoskrnl.exe").c_str()) && _stricmp(name, OBF("ntkrnlmp.exe").c_str()))
                continue;
            const auto base = reinterpret_cast<std::uint64_t>(entry.base);
            if (base < kernel_min || (base & 4095))
                throw std::runtime_error("Kernel module base is unavailable under the current token");
            return base;
        }
        break;
    }
    throw std::runtime_error("Running NT kernel is absent from the module query");
}

std::uint64_t value(std::uint64_t caller_cr3, std::uint64_t address) {
    std::uint64_t output = 0;
    if (address < kernel_min || read(caller_cr3, caller_cr3, address, &output, sizeof(output)) != sizeof(output))
        throw std::runtime_error("Kernel process chain is unreadable");
    return output;
}
bool pointer(std::uint64_t address) noexcept { return address >= kernel_min && !(address & 7); }

struct ListProof {
    std::uint64_t head = 0, own_process = 0;
    std::uint32_t links = 0;
};
ListProof check_list(const KernelImage& image, std::uint64_t cr3, std::uint64_t system_process,
                     std::uint32_t offset, std::uint32_t pid_offset, std::uint32_t peb_offset,
                     std::uint32_t created_offset, memory::ProcessIdentity own, std::uint64_t own_peb,
                     std::uint64_t system_created) {
    const auto first = system_process + offset;
    auto current = first;
    std::set<std::uint64_t> seen;
    ListProof result{.links = offset};
    do {
        if (!pointer(current) || !seen.insert(current).second || seen.size() > 65536)
            throw std::runtime_error("Malformed kernel process list");
        const auto next = value(cr3, current);
        const auto previous = value(cr3, current + 8);
        if (!pointer(next) || !pointer(previous) || value(cr3, next + 8) != current ||
            value(cr3, previous) != current)
            throw std::runtime_error("Unstable kernel process list links");
        if (image.writable_data(current)) {
            if (result.head)
                throw std::runtime_error("Multiple kernel process list heads");
            result.head = current;
        } else {
            if (current < kernel_min + offset)
                throw std::runtime_error("Invalid kernel process object address");
            const auto object = current - offset;
            const auto pid = value(cr3, object + pid_offset);
            const auto created = value(cr3, object + created_offset);
            if (!pid || pid > UINT32_MAX || !created)
                throw std::runtime_error("Invalid kernel process identity");
            if (current == first) {
                if (object != system_process || pid != 4 || created != system_created ||
                    value(cr3, object + peb_offset) != 0)
                    throw std::runtime_error("System process identity changed");
            } else if (pid == 4) {
                throw std::runtime_error("Duplicate System process");
            }
            if (pid == own.pid) {
                if (result.own_process || created != own.creation_time ||
                    value(cr3, object + peb_offset) != own_peb)
                    throw std::runtime_error("Reader process identity mismatch");
                result.own_process = object;
            }
        }
        current = next;
    } while (current != first);
    if (!result.head || !result.own_process || seen.size() < 3)
        throw std::runtime_error("Process list lacks required independent anchors");
    return result;
}
ListProof infer_list(const KernelImage& image, std::uint64_t cr3, std::uint64_t system_process,
                     std::uint32_t pid_offset, std::uint32_t peb_offset, std::uint32_t created_offset,
                     memory::ProcessIdentity own, std::uint64_t own_peb, std::uint64_t system_created) {
    // 运行中内核的 EPROCESS 将 ActiveProcessLinks 紧邻 UniqueProcessId。
    // 只接受该结构关系，经完整双向链、映像内哨兵和自身进程核验；布局变化则拒绝。
    if (pid_offset > 0x1000 - 24)
        throw std::runtime_error("Kernel process list field exceeds supported prefix");
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        try {
            return check_list(image, cr3, system_process, pid_offset + 8, pid_offset, peb_offset,
                              created_offset, own, own_peb, system_created);
        } catch (const std::runtime_error&) {
            if (attempt == 2)
                throw;
            Sleep(1); // 进程加入或退出可能让两次链表读取跨越同一次修改。
        }
    }
    throw std::runtime_error("Unreachable kernel process list state");
}
std::uint32_t infer_cr3(std::uint64_t caller_cr3, const KernelImage& image, std::uint64_t system_process,
                        std::uint64_t own_process, std::size_t prefix_bytes) {
    std::vector<std::uint8_t> system(prefix_bytes), own(prefix_bytes);
    if (read(caller_cr3, caller_cr3, system_process, system.data(), system.size()) != system.size() ||
        read(caller_cr3, caller_cr3, own_process, own.data(), own.size()) != own.size())
        throw std::runtime_error("Kernel process prefix changed during CR3 inference");
    const std::array<std::uint64_t, 2> marker{0xB74C39E50D218FA6ULL, 0x63EFA92D48105C77ULL};
    std::array<std::uint64_t, 2> observed{};
    IMAGE_DOS_HEADER dos{};
    std::optional<std::uint32_t> match;
    for (std::uint32_t offset = 0; offset + 8 <= prefix_bytes; offset += 8) {
        std::uint64_t system_root = 0, own_root = 0;
        std::memcpy(&system_root, system.data() + offset, 8);
        std::memcpy(&own_root, own.data() + offset, 8);
        if (!system_root || !own_root || (system_root & 4095) || (own_root & 4095) ||
            system_root >= 0x000FFFFFFFFFF000ULL || own_root >= 0x000FFFFFFFFFF000ULL)
            continue;
        if (read(caller_cr3, own_root, reinterpret_cast<std::uint64_t>(marker.data()), observed.data(),
                 sizeof(observed)) != sizeof(observed) || observed != marker ||
            read(caller_cr3, system_root, image.base(), &dos, sizeof(dos)) != sizeof(dos) ||
            dos.e_magic != IMAGE_DOS_SIGNATURE)
            continue;
        if (match)
            throw std::runtime_error("Ambiguous kernel DirectoryTableBase offset");
        match = offset;
    }
    if (!match)
        throw std::runtime_error("Cannot verify kernel DirectoryTableBase offset");
    return *match;
}
KernelLayout infer_layout(std::uint64_t caller_cr3) {
    const KernelImage image(caller_cr3, kernel_base());
    KernelLayout result;
    result.pid = image.accessor_offset("PsGetProcessId");
    result.peb = image.accessor_offset("PsGetProcessPeb");
    result.created = image.accessor_offset("PsGetProcessCreateTimeQuadPart");
    if (result.pid == result.peb || result.pid == result.created || result.peb == result.created)
        throw std::runtime_error("Kernel process accessors have overlapping fields");
    const auto initial = image.export_address("PsInitialSystemProcess", false);
    const auto system_process = value(caller_cr3, initial);
    if (!pointer(system_process) || value(caller_cr3, system_process + result.pid) != 4 ||
        value(caller_cr3, system_process + result.peb) != 0)
        throw std::runtime_error("Invalid PsInitialSystemProcess anchor");
    const auto own_pid = GetCurrentProcessId();
    const auto own_peb = __readgsqword(0x60);
    if (!memory::user_range(own_peb))
        throw std::runtime_error("Reader PEB is unavailable");
    std::optional<memory::ProcessIdentity> own, system;
    for (const auto& entry : platform::windows::query_processes()) {
        if (entry.identity.pid == own_pid) own = entry.identity;
        if (entry.identity.pid == 4) system = entry.identity;
    }
    if (!own || !system || system->creation_time != value(caller_cr3, system_process + result.created))
        throw std::runtime_error("Kernel process anchors disagree with the native process catalog");
    const auto links = infer_list(image, caller_cr3, system_process, result.pid, result.peb, result.created,
                                  *own, own_peb, system->creation_time);
    result.links = links.links;
    result.process_head = links.head;
    result.cr3 = infer_cr3(caller_cr3, image, system_process, links.own_process,
                           static_cast<std::size_t>(std::max({result.pid, result.peb, result.created}) + 8));
    if (value(caller_cr3, system_process + result.pid) != 4 ||
        value(caller_cr3, links.own_process + result.pid) != own_pid ||
        value(caller_cr3, links.own_process + result.created) != own->creation_time ||
        value(caller_cr3, result.process_head + 8) < kernel_min)
        throw std::runtime_error("Kernel process layout changed before publication");
    return result;
}
} // namespace

KernelLayout resolve_kernel_layout(std::uint64_t caller_cr3) {
    // 缓存成功结果和确定失败；不为每次 CS2 重绑重复进行系统模块查询。
    static std::mutex mutex;
    static std::optional<KernelLayout> cached;
    static std::string failure;
    const std::lock_guard lock(mutex);
    if (!cached && failure.empty()) {
        try {
            cached = infer_layout(caller_cr3);
        } catch (const std::exception& error) {
            failure = error.what();
        }
    }
    if (!cached)
        throw std::runtime_error("Kernel layout inference failed: " + failure);
    return *cached;
}
ProcessBinding resolve_process(const KernelLayout& layout, memory::ProcessIdentity process, std::uint64_t caller_cr3) {
    const auto head = layout.process_head;
    auto link = value(caller_cr3, head);
    std::set<std::uint64_t> seen;
    while (link != head) {
        if (!pointer(link) || link < kernel_min + layout.links || !seen.insert(link).second || seen.size() > 65536)
            throw std::runtime_error("Invalid kernel process list");
        const auto object = link - layout.links;
        if (value(caller_cr3, object + layout.pid) == process.pid) {
            const auto created = value(caller_cr3, object + layout.created);
            const ProcessBinding result{value(caller_cr3, object + layout.cr3), value(caller_cr3, object + layout.peb)};
            if (created != process.creation_time || value(caller_cr3, object + layout.pid) != process.pid ||
                value(caller_cr3, object + layout.created) != created || !result.cr3 || (result.cr3 & 4095) ||
                !memory::user_range(result.peb))
                throw std::runtime_error("Process instance changed during binding");
            return result;
        }
        link = value(caller_cr3, link);
    }
    throw std::runtime_error("Target process is absent from the kernel process list");
}
} // namespace cs2::backends::hyperv::native
