#include "backends/hyperv/native/kernel_image.hpp"
#include "backends/hyperv/native/hypercall.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace cs2::backends::hyperv::native {
namespace {
constexpr std::uint64_t kernel_min = 0xFFFF800000000000ULL;
}
void KernelImage::exact(std::uint64_t address, void* destination, std::size_t size) const {
    if (read(cr3_, cr3_, address, destination, size) != size)
        throw std::runtime_error("Running kernel image is unreadable");
}
bool KernelImage::range(std::uint64_t rva, std::uint64_t bytes) const noexcept {
    return rva < nt_.OptionalHeader.SizeOfImage && bytes <= nt_.OptionalHeader.SizeOfImage - rva;
}
const IMAGE_SECTION_HEADER* KernelImage::section(std::uint32_t rva) const noexcept {
    for (const auto& section : sections_) {
        const auto start = section.VirtualAddress;
        const auto length = std::max(section.Misc.VirtualSize, section.SizeOfRawData);
        if (rva >= start && rva - start < length)
            return &section;
    }
    return nullptr;
}
bool KernelImage::writable_data(std::uint64_t address) const noexcept {
    if (address < base_ || address - base_ > std::numeric_limits<std::uint32_t>::max())
        return false;
    const auto* entry = section(static_cast<std::uint32_t>(address - base_));
    return entry && (entry->Characteristics & IMAGE_SCN_MEM_WRITE) &&
           (entry->Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) &&
           !(entry->Characteristics & IMAGE_SCN_MEM_EXECUTE);
}
std::string KernelImage::ascii(std::uint32_t rva) const {
    std::string result;
    for (std::uint32_t i = 0; i < 128;) {
        if (!range(static_cast<std::uint64_t>(rva) + i, 1))
            break;
        const auto left = std::min<std::size_t>({128 - i, 4096 - ((rva + i) & 4095),
                                                nt_.OptionalHeader.SizeOfImage - (rva + i)});
        std::array<char, 128> buffer{};
        exact(base_ + rva + i, buffer.data(), left);
        for (std::size_t j = 0; j < left; ++j) {
            if (buffer[j] == '\0')
                return result;
            if (static_cast<unsigned char>(buffer[j]) < 0x20 ||
                static_cast<unsigned char>(buffer[j]) > 0x7e)
                throw std::runtime_error("Invalid kernel export name");
            result.push_back(buffer[j]);
        }
        i += static_cast<std::uint32_t>(left);
    }
    throw std::runtime_error("Unterminated kernel export name");
}
KernelImage::KernelImage(std::uint64_t caller_cr3, std::uint64_t base) : cr3_(caller_cr3), base_(base) {
    if (base < kernel_min || (base & 4095))
        throw std::runtime_error("Invalid running kernel base");
    const auto dos = value<IMAGE_DOS_HEADER>(base);
    if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0x40 || dos.e_lfanew >= 0x1000)
        throw std::runtime_error("Invalid running kernel DOS header");
    nt_ = value<IMAGE_NT_HEADERS64>(base + dos.e_lfanew);
    if (nt_.Signature != IMAGE_NT_SIGNATURE || nt_.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        !(nt_.FileHeader.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) ||
        nt_.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) ||
        nt_.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt_.OptionalHeader.Subsystem != IMAGE_SUBSYSTEM_NATIVE ||
        nt_.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT ||
        nt_.FileHeader.NumberOfSections == 0 || nt_.FileHeader.NumberOfSections > 96 ||
        nt_.OptionalHeader.SizeOfImage < 0x100000 || nt_.OptionalHeader.SizeOfImage > 0x10000000 ||
        nt_.OptionalHeader.SizeOfHeaders > nt_.OptionalHeader.SizeOfImage)
        throw std::runtime_error("Invalid running kernel PE header");
    const auto section_offset = static_cast<std::uint64_t>(dos.e_lfanew) + sizeof(DWORD) +
                                sizeof(IMAGE_FILE_HEADER) + nt_.FileHeader.SizeOfOptionalHeader;
    const auto section_bytes = static_cast<std::uint64_t>(nt_.FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
    if (section_offset + section_bytes > nt_.OptionalHeader.SizeOfHeaders || !range(section_offset, section_bytes))
        throw std::runtime_error("Invalid running kernel section table");
    sections_.resize(nt_.FileHeader.NumberOfSections);
    exact(base + section_offset, sections_.data(), section_bytes);
    for (const auto& entry : sections_)
        if (!entry.Misc.VirtualSize || !range(entry.VirtualAddress,
                                              std::max(entry.Misc.VirtualSize, entry.SizeOfRawData)))
            throw std::runtime_error("Invalid running kernel section range");
    export_range_ = nt_.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!range(export_range_.VirtualAddress, export_range_.Size) ||
        export_range_.Size < sizeof(IMAGE_EXPORT_DIRECTORY) ||
        !section(export_range_.VirtualAddress))
        throw std::runtime_error("Invalid running kernel export directory");
    exports_ = value<IMAGE_EXPORT_DIRECTORY>(base + export_range_.VirtualAddress);
    const auto export_name = ascii(exports_.Name);
    if (export_name != "ntoskrnl.exe" && export_name != "ntkrnlmp.exe")
        throw std::runtime_error("Queried module is not the running NT kernel");
    if (!exports_.NumberOfNames || exports_.NumberOfNames > 65536 ||
        !exports_.NumberOfFunctions || exports_.NumberOfFunctions > 65536 ||
        !range(exports_.AddressOfNames, static_cast<std::uint64_t>(exports_.NumberOfNames) * 4) ||
        !range(exports_.AddressOfNameOrdinals, static_cast<std::uint64_t>(exports_.NumberOfNames) * 2) ||
        !range(exports_.AddressOfFunctions, static_cast<std::uint64_t>(exports_.NumberOfFunctions) * 4))
        throw std::runtime_error("Invalid running kernel export tables");
}
std::uint64_t KernelImage::export_address(std::string_view name, bool executable) const {
    std::uint32_t low = 0, high = exports_.NumberOfNames;
    while (low < high) {
        const auto middle = low + (high - low) / 2;
        const auto name_rva = value<std::uint32_t>(base_ + exports_.AddressOfNames + 4ULL * middle);
        const auto comparison = ascii(name_rva).compare(name);
        if (comparison < 0)
            low = middle + 1;
        else
            high = middle;
    }
    if (low == exports_.NumberOfNames ||
        ascii(value<std::uint32_t>(base_ + exports_.AddressOfNames + 4ULL * low)) != name)
        throw std::runtime_error("Required running kernel export is absent: " + std::string(name));
    if (low + 1 < exports_.NumberOfNames &&
        ascii(value<std::uint32_t>(base_ + exports_.AddressOfNames + 4ULL * (low + 1))) == name)
        throw std::runtime_error("Ambiguous running kernel export");
    const auto ordinal = value<std::uint16_t>(base_ + exports_.AddressOfNameOrdinals + 2ULL * low);
    if (ordinal >= exports_.NumberOfFunctions)
        throw std::runtime_error("Invalid running kernel export ordinal");
    const auto rva = value<std::uint32_t>(base_ + exports_.AddressOfFunctions + 4ULL * ordinal);
    if (!range(rva, 8) ||
        (rva >= export_range_.VirtualAddress && rva - export_range_.VirtualAddress < export_range_.Size))
        throw std::runtime_error("Invalid or forwarded running kernel export");
    const auto* entry = section(rva);
    if (!entry || (executable && !(entry->Characteristics & IMAGE_SCN_MEM_EXECUTE)) ||
        (!executable && !(entry->Characteristics & IMAGE_SCN_MEM_READ)))
        throw std::runtime_error("Running kernel export has the wrong section type");
    return base_ + rva;
}
std::uint32_t KernelImage::accessor_offset(std::string_view name) const {
    const auto address = export_address(name, true);
    std::array<std::uint8_t, 8> code{};
    exact(address, code.data(), code.size());
    std::uint32_t offset = 0;
    if (code[0] == 0x48 && code[1] == 0x8B && code[2] == 0x81 && code[7] == 0xC3)
        std::memcpy(&offset, code.data() + 3, 4); // mov rax,[rcx+disp32]; ret
    else if (code[0] == 0x48 && code[1] == 0x8B && code[2] == 0x41 && code[4] == 0xC3 &&
             code[3] < 0x80)
        offset = code[3]; // mov rax,[rcx+disp8]; ret
    else if (code[0] == 0x48 && code[1] == 0x8B && code[2] == 0x01 && code[3] == 0xC3)
        offset = 0; // mov rax,[rcx]; ret
    else
        throw std::runtime_error("Unrecognized running kernel accessor: " + std::string(name));
    if (offset > 0x1000 || (offset & 7))
        throw std::runtime_error("Invalid running kernel accessor offset");
    return offset;
}
} // namespace cs2::backends::hyperv::native
