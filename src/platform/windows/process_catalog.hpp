#pragma once
#include "memory/session.hpp"
#include "platform/windows/native_api.hpp"
#include "platform/windows/process_name.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <vector>

namespace cs2::platform::windows {
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

struct ProcessCatalogStats {
    std::uint64_t length_queries = 0, snapshot_queries = 0, successful_snapshots = 0;
    std::size_t buffer_bytes = 0;
};

// 本机快照仅在首次访问或显式失效后刷新。运行中的目标身份由后端核验，
// 缓存中的相同 PID/创建时间不能作为存活证明。
class ProcessCatalog {
  public:
    void invalidate() { const std::lock_guard lock(mutex_); valid_ = false; }
    ProcessCatalogStats stats() const {
        const std::lock_guard lock(mutex_);
        auto result = stats_;
        result.buffer_bytes = buffer_.size();
        return result;
    }
    std::optional<memory::ProcessIdentity> identity(std::uint32_t pid) {
        const std::lock_guard lock(mutex_);
        ensure();
        std::optional<memory::ProcessIdentity> result;
        visit([&](const SystemProcessPrefix& entry, std::size_t, std::size_t) {
            if (reinterpret_cast<std::uintptr_t>(entry.pid) != pid) return true;
            if (pid && entry.created.QuadPart > 0)
                result = memory::ProcessIdentity{pid, static_cast<std::uint64_t>(entry.created.QuadPart)};
            return false; // 仅查身份，不构造全表记录，也不解析其他进程名。
        });
        return result;
    }
    std::optional<memory::ProcessIdentity> find(ProcessNameKey name, std::optional<std::uint32_t> selected = {}) {
        const std::lock_guard lock(mutex_);
        ensure();
        std::optional<memory::ProcessIdentity> found;
        visit([&](const SystemProcessPrefix& entry, std::size_t offset, std::size_t extent) {
            const auto pid = reinterpret_cast<std::uintptr_t>(entry.pid);
            if (!pid || pid > UINT32_MAX || entry.created.QuadPart <= 0 || (selected && pid != *selected))
                return true;
            const auto address = reinterpret_cast<std::uintptr_t>(entry.image.Buffer);
            const auto start = reinterpret_cast<std::uintptr_t>(buffer_.data()) + offset;
            if (entry.image.Length % sizeof(wchar_t) || entry.image.Length > entry.image.MaximumLength ||
                (entry.image.Length && (address < start || address - start > extent ||
                                       entry.image.Length > extent - (address - start))))
                throw std::runtime_error("Invalid native process name range");
            const std::wstring_view image(entry.image.Buffer, entry.image.Length / sizeof(wchar_t));
            if (image.find(L'\0') != image.npos)
                throw std::runtime_error("Embedded null in process name");
            if (hash_process_name(image) == name) {
                if (found) throw std::runtime_error("Multiple matching process instances; use --pid explicitly");
                found = memory::ProcessIdentity{static_cast<std::uint32_t>(pid),
                                                static_cast<std::uint64_t>(entry.created.QuadPart)};
            } else if (selected)
                throw std::runtime_error("Selected PID does not match the configured executable name");
            return !selected;
        });
        return found;
    }
  private:
    void grow(std::size_t required) {
        constexpr std::size_t limit = 256ULL * 1024 * 1024, unit = 64 * 1024;
        if (!required || required > limit) throw std::runtime_error("Native process list exceeds resource boundary");
        const auto extra = std::max(unit, required / 4);
        const auto capacity = std::min(limit, required + std::min(extra, limit - required));
        buffer_.resize(std::min(limit, (capacity + unit - 1) / unit * unit));
    }
    void ensure() {
        if (valid_) return;
        if (buffer_.empty()) {
            ULONG required = 0;
            ++stats_.length_queries;
            const auto status = ::NtQuerySystemInformation(static_cast<SYSTEM_INFORMATION_CLASS>(5), nullptr, 0, &required);
            if (status != status_length_mismatch && status != status_buffer_too_small && !nt_success(status))
                throw std::runtime_error("Native process length query failed");
            grow(required);
        }
        for (unsigned attempt = 0; attempt < 8; ++attempt) {
            ULONG returned = 0;
            ++stats_.snapshot_queries;
            const auto status = ::NtQuerySystemInformation(static_cast<SYSTEM_INFORMATION_CLASS>(5), buffer_.data(),
                                                           static_cast<ULONG>(buffer_.size()), &returned);
            if (status == status_length_mismatch || status == status_buffer_too_small) {
                grow(std::max<std::size_t>(returned, buffer_.size() + buffer_.size() / 2));
                continue;
            }
            if (!nt_success(status)) throw std::runtime_error("Native process snapshot query failed");
            if (returned < sizeof(SystemProcessPrefix) || returned > buffer_.size())
                throw std::runtime_error("Invalid native process list length");
            used_ = returned;
            ++stats_.successful_snapshots;
            valid_ = true;
            return;
        }
        throw std::runtime_error("Native process list changed beyond retry budget");
    }
    template<class Visitor> void visit(Visitor&& visitor) const {
        for (std::size_t offset = 0;;) {
            if (offset > used_ || sizeof(SystemProcessPrefix) > used_ - offset)
                throw std::runtime_error("Truncated native process entry");
            SystemProcessPrefix entry{};
            std::memcpy(&entry, buffer_.data() + offset, sizeof(entry));
            const auto extent = entry.next ? static_cast<std::size_t>(entry.next) : used_ - offset;
            if (extent < sizeof(entry) || extent > used_ - offset || (entry.next && entry.next % 8))
                throw std::runtime_error("Invalid native process entry stride");
            if (!visitor(entry, offset, extent) || !entry.next) return;
            offset += entry.next;
        }
    }
    mutable std::mutex mutex_;
    std::vector<std::byte> buffer_;
    std::size_t used_ = 0;
    bool valid_ = false;
    ProcessCatalogStats stats_;
};
inline ProcessCatalog& process_catalog() { static ProcessCatalog catalog; return catalog; }
inline void invalidate_process_catalog() { process_catalog().invalidate(); }
inline ProcessCatalogStats process_catalog_stats() { return process_catalog().stats(); }
inline std::optional<memory::ProcessIdentity> find_process(ProcessNameKey name,
                                                         std::optional<std::uint32_t> selected = {}) {
    return process_catalog().find(name, selected);
}
inline std::optional<memory::ProcessIdentity> query_identity(std::uint32_t pid) {
    return process_catalog().identity(pid);
}
} // namespace cs2::platform::windows
