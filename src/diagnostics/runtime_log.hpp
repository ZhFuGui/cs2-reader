#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <ostream>
#include <streambuf>
#include <string>
#include <vector>
#include <windows.h>

namespace cs2::diagnostics {
// 默认仅保留最近 1MiB。日志不接管标准流；帮助文本仍可使用正常的 stdout。
class RuntimeLog final : private std::streambuf {
  public:
    explicit RuntimeLog(std::size_t capacity = 1024 * 1024);
    ~RuntimeLog() override;
    RuntimeLog(const RuntimeLog&) = delete;
    RuntimeLog& operator=(const RuntimeLog&) = delete;
    std::ostream& stream() noexcept { return stream_; }
    void enable_file(); // 仅显式请求时，在当前进程 %TEMP% 下排他创建随机名文件。
    std::string snapshot() const;
    std::filesystem::path file_path() const;
    std::uint32_t file_error() const;
    std::uint64_t overwritten_bytes() const;

  private:
    std::streamsize xsputn(const char*, std::streamsize) override;
    int_type overflow(int_type) override;
    int sync() override;
    void append_ring(const char*, std::size_t) noexcept;
    void append_file(const char*, std::size_t) noexcept;
    void flush_file() noexcept;
    mutable std::mutex mutex_;
    std::vector<char> ring_;
    std::size_t head_ = 0, size_ = 0;
    std::uint64_t overwritten_ = 0;
    HANDLE file_ = INVALID_HANDLE_VALUE;
    std::filesystem::path path_;
    std::array<char, 4096> pending_{};
    std::size_t pending_size_ = 0;
    std::uint32_t file_error_ = 0;
    std::ostream stream_;
};
} // namespace cs2::diagnostics
