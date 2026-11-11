#include "backends/hyperv/native/kernel_binding.hpp"
#include "backends/hyperv/native/hypercall.hpp"
#include "diagnostics/obf.hpp"
#include "platform/windows/native_api.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <dbghelp.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <vector>

extern "C" __declspec(dllimport) NTSTATUS NTAPI RtlAdjustPrivilege(ULONG, BOOLEAN, BOOLEAN, PBOOLEAN);
namespace cs2::backends::hyperv::native {
namespace {
struct ModuleEntry {
    HANDLE section;
    void* mapped;
    void* base;
    ULONG size, flags;
    USHORT load, init, count, name_offset;
    unsigned char path[256];
};
struct Modules {
    ULONG count;
    ModuleEntry entries[1];
};
struct Library {
    HMODULE value = nullptr;
    explicit Library(const std::filesystem::path& path)
        : value(LoadLibraryExW(path.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)) {
        if (!value)
            throw std::runtime_error("Cannot load Windows SDK debugging library");
    }
    ~Library() {
        if (value)
            FreeLibrary(value);
    }
};
template <class T> T function(HMODULE module, const char* name) {
    const auto result = GetProcAddress(module, name);
    if (!result)
        throw std::runtime_error(std::string("Missing debugging export: ") + name);
    return reinterpret_cast<T>(result);
}
std::filesystem::path debugger_directory() {
    wchar_t path[32768]{};
    DWORD bytes = sizeof(path);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots", L"KitsRoot10",
                     RRF_RT_REG_SZ, nullptr, path, &bytes) != ERROR_SUCCESS)
        throw std::runtime_error("Kernel binding requires the Windows SDK debugging tools");
    return std::filesystem::path(path) / L"Debuggers" / L"x64";
}
std::uint64_t kernel_base() {
    BOOLEAN previous = FALSE;
    const auto privilege = RtlAdjustPrivilege(20, TRUE, FALSE, &previous);
    struct Restore {
        NTSTATUS status;
        BOOLEAN previous;
        ~Restore() {
            if (status >= 0) {
                BOOLEAN ignored;
                RtlAdjustPrivilege(20, previous, FALSE, &ignored);
            }
        }
    } guard{privilege, previous};
    ULONG required = 0;
    ::NtQuerySystemInformation(static_cast<SYSTEM_INFORMATION_CLASS>(11), nullptr, 0, &required);
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        if (required < offsetof(Modules, entries) || required > 16 * 1024 * 1024)
            throw std::runtime_error("Invalid kernel module buffer size");
        std::vector<std::byte> buffer(static_cast<std::size_t>(required) + 16384);
        const auto status = ::NtQuerySystemInformation(static_cast<SYSTEM_INFORMATION_CLASS>(11), buffer.data(),
                                                       static_cast<ULONG>(buffer.size()), &required);
        if (status == platform::windows::status_length_mismatch)
            continue;
        if (status < 0 || required > buffer.size())
            throw std::runtime_error("Native kernel module query failed");
        ULONG count = 0;
        std::memcpy(&count, buffer.data(), sizeof(count));
        if (required < offsetof(Modules, entries) ||
            count > (required - offsetof(Modules, entries)) / sizeof(ModuleEntry))
            throw std::runtime_error("Invalid kernel module array");
        for (ULONG i = 0; i < count; ++i) {
            ModuleEntry entry{};
            std::memcpy(&entry, buffer.data() + offsetof(Modules, entries) + i * sizeof(entry), sizeof(entry));
            if (entry.name_offset >= sizeof(entry.path))
                continue;
            const auto* name = reinterpret_cast<const char*>(entry.path) + entry.name_offset;
            if (!std::memchr(name, 0, sizeof(entry.path) - entry.name_offset))
                continue;
            if (_stricmp(name, OBF("ntoskrnl.exe").c_str()) && _stricmp(name, OBF("ntkrnlmp.exe").c_str()))
                continue;
            const auto base = reinterpret_cast<std::uint64_t>(entry.base);
            if (base < 0xFFFF800000000000ULL || (base & 4095))
                throw std::runtime_error("Kernel address is unavailable; run the hypervisor mode elevated");
            return base;
        }
        break;
    }
    throw std::runtime_error("Cannot locate the current kernel image");
}
} // namespace
KernelLayout resolve_kernel_layout(std::uint64_t caller_cr3) {
    static std::mutex mutex;
    const std::lock_guard lock(mutex);
    const auto base = kernel_base();
    const auto directory = debugger_directory();
    Library server(directory / L"symsrv.dll"), library(directory / L"dbghelp.dll");
    const auto init = function<decltype(&SymInitializeW)>(library.value, "SymInitializeW");
    const auto cleanup = function<decltype(&SymCleanup)>(library.value, "SymCleanup");
    const auto load = function<decltype(&SymLoadModuleExW)>(library.value, "SymLoadModuleExW");
    const auto find = function<decltype(&SymFromName)>(library.value, "SymFromName");
    const auto type = function<decltype(&SymGetTypeFromName)>(library.value, "SymGetTypeFromName");
    const auto info = function<decltype(&SymGetTypeInfo)>(library.value, "SymGetTypeInfo");
    const auto options = function<decltype(&SymSetOptions)>(library.value, "SymSetOptions");
    const auto get_options = function<decltype(&SymGetOptions)>(library.value, "SymGetOptions");
    const auto old_options = get_options();
    options(SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
    struct Restore {
        decltype(options) fn;
        DWORD value;
        ~Restore() { fn(value); }
    } restore{options, old_options};
    wchar_t local[32768]{};
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local))))
        throw std::runtime_error("LOCALAPPDATA unavailable for symbol cache");
    const auto cache = std::filesystem::path(local) / L"Symbols";
    std::filesystem::create_directories(cache);
    const auto path = L"srv*" + cache.wstring() + L"*https://msdl.microsoft.com/download/symbols";
    const auto process = GetCurrentProcess();
    if (!init(process, path.c_str(), FALSE))
        throw std::runtime_error("SymInitializeW failed");
    struct Cleanup {
        decltype(cleanup) fn;
        HANDLE process;
        ~Cleanup() { fn(process); }
    } guard{cleanup, process};
    wchar_t system[MAX_PATH]{};
    if (!GetSystemDirectoryW(system, MAX_PATH))
        throw std::runtime_error("System directory unavailable");
    const auto image = std::filesystem::path(system) / WOBF_W(L"ntoskrnl.exe");
    // 系统更新后磁盘内核可能不同于本次启动的内核。匹配之前不使用其 PDB 布局。
    std::ifstream file(image, std::ios::binary);
    IMAGE_DOS_HEADER disk_dos{}, live_dos{};
    file.read(reinterpret_cast<char*>(&disk_dos), sizeof(disk_dos));
    if (!file || read(caller_cr3, caller_cr3, base, &live_dos, sizeof(live_dos)) != sizeof(live_dos) ||
        disk_dos.e_magic != IMAGE_DOS_SIGNATURE || live_dos.e_magic != IMAGE_DOS_SIGNATURE ||
        disk_dos.e_lfanew < 0x40 || live_dos.e_lfanew != disk_dos.e_lfanew || disk_dos.e_lfanew > 0x100000)
        throw std::runtime_error("Cannot match the running kernel to its on-disk image");
    IMAGE_NT_HEADERS64 disk_nt{}, live_nt{};
    file.seekg(disk_dos.e_lfanew);
    file.read(reinterpret_cast<char*>(&disk_nt), sizeof(disk_nt));
    if (!file || read(caller_cr3, caller_cr3, base + live_dos.e_lfanew, &live_nt, sizeof(live_nt)) != sizeof(live_nt) ||
        disk_nt.Signature != IMAGE_NT_SIGNATURE || live_nt.Signature != IMAGE_NT_SIGNATURE ||
        disk_nt.FileHeader.TimeDateStamp != live_nt.FileHeader.TimeDateStamp ||
        disk_nt.OptionalHeader.SizeOfImage != live_nt.OptionalHeader.SizeOfImage ||
        disk_nt.OptionalHeader.CheckSum != live_nt.OptionalHeader.CheckSum)
        throw std::runtime_error("Running kernel differs from the image used for symbols; refusing guessed offsets");
    const auto loaded = load(process, nullptr, image.c_str(), L"nt", 0x10000000, 0, nullptr, 0);
    if (!loaded)
        throw std::runtime_error("Cannot load matching kernel PDB");
    alignas(SYMBOL_INFO) std::array<unsigned char, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> storage{};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage.data());
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    if (!find(process, "nt!PsActiveProcessHead", symbol) || symbol->Address < loaded ||
        symbol->Address - loaded > 0x20000000)
        throw std::runtime_error("PsActiveProcessHead missing from matching kernel symbols");
    KernelLayout result;
    result.process_head = base + symbol->Address - loaded;
    auto member = [&](const char* structure, const wchar_t* field) -> std::uint32_t {
        if (!type(process, loaded, structure, symbol))
            throw std::runtime_error(std::string("Kernel type unavailable: ") + structure);
        const auto index = symbol->TypeIndex;
        DWORD count = 0;
        if (!info(process, loaded, index, TI_GET_CHILDRENCOUNT, &count) || count > 65536)
            throw std::runtime_error("Invalid kernel type members");
        std::vector<std::byte> children(offsetof(TI_FINDCHILDREN_PARAMS, ChildId) + count * sizeof(ULONG));
        auto* query = reinterpret_cast<TI_FINDCHILDREN_PARAMS*>(children.data());
        query->Count = count;
        query->Start = 0;
        if (!info(process, loaded, index, TI_FINDCHILDREN, query))
            throw std::runtime_error("Cannot enumerate kernel type members");
        for (DWORD i = 0; i < count; ++i) {
            PWSTR name = nullptr;
            if (!info(process, loaded, query->ChildId[i], TI_GET_SYMNAME, &name))
                continue;
            const bool matches = name && std::wcscmp(name, field) == 0;
            if (name)
                LocalFree(name);
            DWORD offset = 0;
            if (matches && info(process, loaded, query->ChildId[i], TI_GET_OFFSET, &offset))
                return offset;
        }
        throw std::runtime_error(std::string("Required kernel field missing in ") + structure);
    };
    result.pid = member("_EPROCESS", L"UniqueProcessId");
    result.links = member("_EPROCESS", L"ActiveProcessLinks");
    result.peb = member("_EPROCESS", L"Peb");
    result.created = member("_EPROCESS", L"CreateTime");
    result.cr3 = member("_EPROCESS", L"Pcb") + member("_KPROCESS", L"DirectoryTableBase");
    return result;
}
ProcessBinding resolve_process(const KernelLayout& layout, memory::ProcessIdentity process, std::uint64_t caller_cr3) {
    auto value = [&](std::uint64_t address) {
        std::uint64_t output = 0;
        if (read(caller_cr3, caller_cr3, address, &output, sizeof(output)) != sizeof(output))
            throw std::runtime_error("Kernel process chain is unreadable");
        return output;
    };
    const auto head = layout.process_head;
    auto link = value(head);
    std::set<std::uint64_t> seen;
    while (link != head) {
        if (link < 0xFFFF800000000000ULL || link < layout.links || !seen.insert(link).second)
            throw std::runtime_error("Invalid kernel process list");
        const auto object = link - layout.links;
        if (value(object + layout.pid) == process.pid) {
            const auto created = value(object + layout.created);
            const ProcessBinding result{value(object + layout.cr3), value(object + layout.peb)};
            if (created != process.creation_time || value(object + layout.pid) != process.pid ||
                value(object + layout.created) != created || !result.cr3 || (result.cr3 & 4095) ||
                !memory::user_range(result.peb))
                throw std::runtime_error("Process instance changed during binding");
            return result;
        }
        link = value(link);
    }
    throw std::runtime_error("Target process is absent from the kernel process list");
}
} // namespace cs2::backends::hyperv::native
