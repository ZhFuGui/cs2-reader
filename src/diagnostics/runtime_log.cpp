#include "diagnostics/runtime_log.hpp"
#include <algorithm>
#include <bcrypt.h>
#include <cstring>
#include <stdexcept>

namespace cs2::diagnostics {
namespace {
std::filesystem::path temporary_directory() {
    const auto required = GetEnvironmentVariableW(L"TEMP", nullptr, 0);
    std::wstring value(required ? required : MAX_PATH, L'\0');
    auto length = required ? GetEnvironmentVariableW(L"TEMP", value.data(), static_cast<DWORD>(value.size()))
                           : GetTempPathW(static_cast<DWORD>(value.size()), value.data());
    if (!length)
        throw std::runtime_error("Cannot resolve temporary log directory");
    if (!required && length >= value.size()) {
        value.resize(static_cast<std::size_t>(length) + 1);
        length = GetTempPathW(static_cast<DWORD>(value.size()), value.data());
    }
    if (!length || length >= value.size())
        throw std::runtime_error("Temporary log directory changed during resolution");
    value.resize(length);
    std::filesystem::path result(value);
    if (!result.is_absolute())
        throw std::runtime_error("Temporary log directory must be absolute");
    return result;
}
std::wstring random_name() {
    std::array<unsigned char, 16> bytes{};
    if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("Cannot generate a temporary log name");
    constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring result; // 不含产品名的随机名：临时目录里的文件名本身不是身份标识。
    result.reserve(32);
    for (auto byte : bytes) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 15]);
    }
    return result + L".log";
}
} // namespace
RuntimeLog::RuntimeLog(std::size_t capacity) : ring_(capacity), stream_(this) {
    if (!capacity)
        throw std::invalid_argument("Log ring capacity must be positive");
}
RuntimeLog::~RuntimeLog() {
    std::lock_guard lock(mutex_);
    flush_file();
    if (file_ != INVALID_HANDLE_VALUE)
        CloseHandle(file_);
}
void RuntimeLog::append_ring(const char* data, std::size_t count) noexcept {
    if (count >= ring_.size()) {
        overwritten_ += size_ + count - ring_.size();
        std::memcpy(ring_.data(), data + count - ring_.size(), ring_.size());
        head_ = 0;
        size_ = ring_.size();
        return;
    }
    const auto removed = count > ring_.size() - size_ ? count - (ring_.size() - size_) : 0;
    head_ = (head_ + removed) % ring_.size();
    size_ -= removed;
    overwritten_ += removed;
    const auto tail = (head_ + size_) % ring_.size();
    const auto first = std::min(count, ring_.size() - tail);
    std::memcpy(ring_.data() + tail, data, first);
    if (count > first)
        std::memcpy(ring_.data(), data + first, count - first);
    size_ += count;
}
void RuntimeLog::flush_file() noexcept {
    if (file_ == INVALID_HANDLE_VALUE || !pending_size_)
        return;
    std::size_t offset = 0;
    while (offset < pending_size_) {
        DWORD written = 0;
        if (!WriteFile(file_, pending_.data() + offset, static_cast<DWORD>(pending_size_ - offset), &written, nullptr) ||
            !written) {
            file_error_ = GetLastError();
            if (!file_error_)
                file_error_ = ERROR_WRITE_FAULT;
            CloseHandle(file_);
            file_ = INVALID_HANDLE_VALUE;
            constexpr char notice[] = "\nLog file write failed; in-memory logging continues.\n";
            append_ring(notice, sizeof(notice) - 1);
            break;
        }
        offset += written;
    }
    pending_size_ = 0;
}
void RuntimeLog::append_file(const char* data, std::size_t count) noexcept {
    while (count && file_ != INVALID_HANDLE_VALUE) {
        const auto chunk = std::min(count, pending_.size() - pending_size_);
        std::memcpy(pending_.data() + pending_size_, data, chunk);
        pending_size_ += chunk;
        data += chunk;
        count -= chunk;
        if (pending_size_ == pending_.size())
            flush_file();
    }
}
std::streamsize RuntimeLog::xsputn(const char* data, std::streamsize count) {
    if (count <= 0)
        return 0;
    std::lock_guard lock(mutex_);
    append_ring(data, static_cast<std::size_t>(count));
    append_file(data, static_cast<std::size_t>(count));
    return count;
}
RuntimeLog::int_type RuntimeLog::overflow(int_type value) {
    if (traits_type::eq_int_type(value, traits_type::eof()))
        return traits_type::not_eof(value);
    const auto character = traits_type::to_char_type(value);
    xsputn(&character, 1);
    return value;
}
int RuntimeLog::sync() {
    std::lock_guard lock(mutex_);
    flush_file();
    return 0; // 文件故障不能置坏整个日志流；环形日志仍继续接收。
}
void RuntimeLog::enable_file() {
    std::lock_guard lock(mutex_);
    if (!path_.empty())
        return;
    const auto directory = temporary_directory();
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        const auto path = directory / random_name();
        const auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
                                        FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            const auto error = GetLastError();
            if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS)
                continue;
            throw std::runtime_error("Cannot create temporary log file: " + std::to_string(error));
        }
        file_ = handle;
        path_ = path;
        const auto first = std::min(size_, ring_.size() - head_);
        append_file(ring_.data() + head_, first);
        if (size_ > first)
            append_file(ring_.data(), size_ - first);
        return;
    }
    throw std::runtime_error("Temporary log name collisions exceeded retry budget");
}
std::string RuntimeLog::snapshot() const {
    std::lock_guard lock(mutex_);
    const auto first = std::min(size_, ring_.size() - head_);
    std::string result(ring_.data() + head_, first);
    result.append(ring_.data(), size_ - first);
    return result;
}
std::filesystem::path RuntimeLog::file_path() const {
    std::lock_guard lock(mutex_);
    return path_;
}
std::uint32_t RuntimeLog::file_error() const {
    std::lock_guard lock(mutex_);
    return file_error_;
}
std::uint64_t RuntimeLog::overwritten_bytes() const {
    std::lock_guard lock(mutex_);
    return overwritten_;
}
} // namespace cs2::diagnostics
