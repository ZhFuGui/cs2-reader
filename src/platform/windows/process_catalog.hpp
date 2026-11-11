#pragma once
#include "memory/session.hpp"
#include "platform/windows/native_api.hpp"
#include "platform/windows/process_name.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace cs2::platform::windows {
struct ProcessRecord {
    memory::ProcessIdentity identity;
    ProcessNameKey name;
};
struct SystemProcessPrefix {
    ULONG next, threads;
    LARGE_INTEGER private_working_set;
    ULONG hard_faults, peak_threads;
    ULONGLONG cycles;
    LARGE_INTEGER created, user, kernel;
    UNICODE_STRING image;
    LONG base_priority;
    HANDLE pid, parent;
};
static_assert(offsetof(SystemProcessPrefix, created) == 0x20 && offsetof(SystemProcessPrefix, image) == 0x38 &&
              offsetof(SystemProcessPrefix, pid) == 0x50 && sizeof(SystemProcessPrefix) == 0x60);

inline std::vector<ProcessRecord> query_processes() {
    std::vector<std::byte> buffer(256 * 1024);
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        ULONG returned = 0;
        const auto status = ::NtQuerySystemInformation(static_cast<SYSTEM_INFORMATION_CLASS>(5), buffer.data(),
                                                       static_cast<ULONG>(buffer.size()), &returned);
        if (status == status_length_mismatch || status == status_buffer_too_small) {
            const auto required = std::max<std::size_t>(returned, buffer.size() * 2);
            if (required > 256ULL * 1024 * 1024)
                throw std::runtime_error("Native process list exceeds resource boundary");
            buffer.resize(required);
            continue;
        }
        if (!nt_success(status))
            throw std::runtime_error("NtQuerySystemInformation(SystemProcessInformation) failed: " +
                                     std::to_string(static_cast<std::uint32_t>(status)));
        if (!returned || returned > buffer.size())
            throw std::runtime_error("Invalid native process list length");
        std::vector<ProcessRecord> result;
        for (std::size_t offset = 0;;) {
            if (offset > returned || sizeof(SystemProcessPrefix) > returned - offset)
                throw std::runtime_error("Truncated native process entry");
            SystemProcessPrefix entry{};
            std::memcpy(&entry, buffer.data() + offset, sizeof(entry));
            const auto extent = entry.next ? static_cast<std::size_t>(entry.next) : returned - offset;
            if (extent < sizeof(entry) || extent > returned - offset || (entry.next && entry.next % 8))
                throw std::runtime_error("Invalid native process entry stride");
            const auto pid = reinterpret_cast<std::uintptr_t>(entry.pid);
            if (entry.image.Length) {
                const auto address = reinterpret_cast<std::uintptr_t>(entry.image.Buffer);
                const auto start = reinterpret_cast<std::uintptr_t>(buffer.data()) + offset;
                if (entry.image.Length % sizeof(wchar_t) || entry.image.Length > entry.image.MaximumLength ||
                    address < start || address - start > extent || entry.image.Length > extent - (address - start))
                    throw std::runtime_error("Invalid native process name range");
                const std::wstring_view name(entry.image.Buffer, entry.image.Length / sizeof(wchar_t));
                if (name.find(L'\0') != name.npos)
                    throw std::runtime_error("Embedded null in process name");
                if (pid && pid <= std::numeric_limits<std::uint32_t>::max() && entry.created.QuadPart)
                    result.push_back(
                        {{static_cast<std::uint32_t>(pid), static_cast<std::uint64_t>(entry.created.QuadPart)},
                         hash_process_name(name)});
            }
            if (!entry.next)
                return result;
            offset += entry.next;
        }
    }
    throw std::runtime_error("Native process list changed beyond retry budget");
}
inline std::optional<memory::ProcessIdentity> find_process(ProcessNameKey name) {
    std::optional<memory::ProcessIdentity> found;
    for (const auto& process : query_processes())
        if (process.name == name) {
            if (found)
                throw std::runtime_error("Multiple matching process instances; use --pid to select explicitly");
            found = process.identity;
        }
    return found;
}
inline std::optional<memory::ProcessIdentity> query_identity(std::uint32_t pid) {
    for (const auto& process : query_processes())
        if (process.identity.pid == pid)
            return process.identity;
    return std::nullopt;
}
} // namespace cs2::platform::windows
