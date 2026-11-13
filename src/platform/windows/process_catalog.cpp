#include "platform/windows/process_catalog.hpp"
#include "memory/read_error.hpp"
#include "platform/windows/native_api.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace cs2::platform::windows {
namespace {
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

} // namespace
template <class Visitor> void ProcessCatalog::visit(Visitor&& visitor) const {
    for (std::size_t offset = 0;;) {
        if (offset > used_ || sizeof(SystemProcessPrefix) > used_ - offset)
            throw std::runtime_error("Truncated native process entry");
        SystemProcessPrefix entry{};
        std::memcpy(&entry, buffer_.data() + offset, sizeof(entry));
        const auto extent = entry.next ? static_cast<std::size_t>(entry.next) : used_ - offset;
        if (extent < sizeof(entry) || extent > used_ - offset || (entry.next && entry.next % 8))
            throw std::runtime_error("Invalid native process entry stride");
        if (!visitor(entry, offset, extent) || !entry.next)
            return;
        offset += entry.next;
    }
}
void ProcessCatalog::grow(std::size_t required) {
    constexpr std::size_t limit = 256ULL * 1024 * 1024, unit = 64 * 1024;
    if (!required || required > limit)
        throw std::runtime_error("Native process list exceeds resource boundary");
    const auto extra = std::max(unit, required / 4);
    const auto capacity = std::min(limit, required + std::min(extra, limit - required));
    buffer_.resize(std::min(limit, (capacity + unit - 1) / unit * unit));
}
void ProcessCatalog::ensure() {
    if (valid_)
        return;
    if (buffer_.empty()) {
        ULONG required = 0;
        ++stats_.length_queries;
        const auto status = ::NtQuerySystemInformation(static_cast<SYSTEM_INFORMATION_CLASS>(5), nullptr, 0, &required);
        if (status != status_length_mismatch && status != status_buffer_too_small && !nt_success(status))
            throw memory::ReadUnavailable("Native process length query failed");
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
        if (!nt_success(status))
            throw memory::ReadUnavailable("Native process snapshot query failed");
        if (returned < sizeof(SystemProcessPrefix) || returned > buffer_.size())
            throw std::runtime_error("Invalid native process list length");
        used_ = returned;
        ++stats_.successful_snapshots;
        valid_ = true;
        return;
    }
    throw memory::ReadUnavailable("Native process list changed beyond retry budget");
}
void ProcessCatalog::invalidate() {
    const std::lock_guard lock(mutex_);
    valid_ = false;
}
memory::ProcessCatalogStats ProcessCatalog::stats() const {
    const std::lock_guard lock(mutex_);
    auto result = stats_;
    result.buffer_bytes = buffer_.size();
    return result;
}
std::optional<memory::ProcessIdentity> ProcessCatalog::identity(std::uint32_t pid) {
    const std::lock_guard lock(mutex_);
    ensure();
    std::optional<memory::ProcessIdentity> result;
    visit([&](const SystemProcessPrefix& entry, std::size_t, std::size_t) {
        if (reinterpret_cast<std::uintptr_t>(entry.pid) != pid)
            return true;
        if (pid && entry.created.QuadPart > 0)
            result = memory::ProcessIdentity{pid, static_cast<std::uint64_t>(entry.created.QuadPart)};
        return false; // 仅查身份，不构造全表记录，也不解析其他进程名。
    });
    return result;
}
std::optional<memory::ProcessIdentity> ProcessCatalog::find(memory::ProcessNameKey name,
                                                            std::optional<std::uint32_t> selected) {
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
            (entry.image.Length &&
             (address < start || address - start > extent || entry.image.Length > extent - (address - start))))
            throw std::runtime_error("Invalid native process name range");
        const std::wstring_view image(entry.image.Buffer, entry.image.Length / sizeof(wchar_t));
        if (image.find(L'\0') != image.npos)
            throw std::runtime_error("Embedded null in process name");
        if (memory::hash_process_name(image) == name) {
            if (found) {
                valid_ = false; // 重叠启动可能只是暂态，下一次重新确认，而不是永久复用歧义快照。
                throw memory::ReadUnavailable("Multiple matching process instances; use --pid explicitly");
            }
            found = memory::ProcessIdentity{static_cast<std::uint32_t>(pid),
                                            static_cast<std::uint64_t>(entry.created.QuadPart)};
        } else if (selected)
            throw std::runtime_error("Selected PID does not match the configured executable name");
        return !selected;
    });
    return found;
}
ProcessCatalog& process_catalog() {
    static ProcessCatalog catalog;
    return catalog;
}
} // namespace cs2::platform::windows
