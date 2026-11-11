#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cs2_reader::v1 {

inline constexpr std::size_t record_header_size = 4;
inline constexpr std::uint32_t maximum_body_size = 16U * 1024U * 1024U;

enum class RecordErrorCode { invalid_length, faulted_decoder, reentrant_operation, truncated_record, ended_stream };

class RecordError final : public std::runtime_error {
  public:
    explicit RecordError(RecordErrorCode code, const char* message) : std::runtime_error(message), code_(code) {}
    [[nodiscard]] RecordErrorCode code() const noexcept { return code_; }

  private:
    RecordErrorCode code_;
};

// 正文是完整的 Envelope protobuf；头部长度不包含四字节头部本身。
[[nodiscard]] inline std::array<std::byte, record_header_size> make_header(std::size_t body_size) {
    if (body_size == 0 || body_size > maximum_body_size)
        throw RecordError(RecordErrorCode::invalid_length, "record body must contain 1..16 MiB bytes");
    const auto length = static_cast<std::uint32_t>(body_size);
    return {std::byte((length >> 24U) & 0xffU), std::byte((length >> 16U) & 0xffU), std::byte((length >> 8U) & 0xffU),
            std::byte(length & 0xffU)};
}

[[nodiscard]] inline std::vector<std::byte> make_record(std::span<const std::byte> body) {
    const auto header = make_header(body.size());
    std::vector<std::byte> record(record_header_size + body.size());
    std::copy(header.begin(), header.end(), record.begin());
    std::copy(body.begin(), body.end(), record.begin() + record_header_size);
    return record;
}

// 每条 TCP 连接独占一个解码器。线程安全由连接所有者保证，回调不得重入或移动/销毁解码器。
// 回调中的正文 span 只在回调期间有效；需跨回调保存时由调用者复制或解码。
// 任意异常后连接应关闭；reset() 仅用于新的字节流，不在损坏流内扫描猜测下一条边界。
class RecordDecoder final {
  public:
    explicit RecordDecoder(std::uint32_t body_limit = maximum_body_size) : body_limit_(body_limit) {
        if (body_limit_ == 0 || body_limit_ > maximum_body_size)
            throw RecordError(RecordErrorCode::invalid_length, "decoder limit must contain 1..16 MiB bytes");
    }
    RecordDecoder(const RecordDecoder&) = delete;
    RecordDecoder& operator=(const RecordDecoder&) = delete;
    RecordDecoder(RecordDecoder&&) = delete;
    RecordDecoder& operator=(RecordDecoder&&) = delete;

    // 支持头部/正文任意拆读，以及一次 read 中包含多条记录。返回本次交付的记录数。
    template <class Callback> std::size_t feed(std::span<const std::byte> input, Callback&& on_record) {
        ensure_idle();
        if (failed_)
            throw RecordError(RecordErrorCode::faulted_decoder, "decoder requires a new connection and reset");
        if (ended_)
            throw RecordError(RecordErrorCode::ended_stream, "stream has already ended");
        BusyScope busy{busy_};
        std::size_t delivered = 0;
        try {
            while (!input.empty()) {
                if (expected_ == 0) {
                    const auto count = std::min(input.size(), header_.size() - header_used_);
                    std::copy_n(input.begin(), count, header_.begin() + header_used_);
                    input = input.subspan(count);
                    header_used_ += count;
                    if (header_used_ != header_.size())
                        continue;
                    for (auto value : header_)
                        expected_ = (expected_ << 8U) | std::to_integer<std::uint32_t>(value);
                    if (expected_ == 0 || expected_ > body_limit_)
                        throw RecordError(RecordErrorCode::invalid_length, "invalid record body length");
                    body_.resize(expected_);
                    body_used_ = 0;
                }
                const auto count = std::min(input.size(), body_.size() - body_used_);
                std::copy_n(input.begin(), count, body_.begin() + body_used_);
                input = input.subspan(count);
                body_used_ += count;
                if (body_used_ != body_.size())
                    continue;
                std::invoke(on_record, std::span<const std::byte>{body_});
                if (failed_)
                    throw RecordError(RecordErrorCode::faulted_decoder, "callback attempted a reentrant operation");
                ++delivered;
                header_used_ = 0;
                expected_ = 0;
                body_used_ = 0;
                body_.clear();
            }
        } catch (...) {
            failed_ = true;
            throw;
        }
        return delivered;
    }

    // TCP 正常 EOF 仍需检查半条记录。异常断线由连接层报告，不把残缺消息交给消费者。
    void finish() {
        ensure_idle();
        if (failed_)
            throw RecordError(RecordErrorCode::faulted_decoder, "decoder requires reset");
        if (incomplete()) {
            failed_ = true;
            throw RecordError(RecordErrorCode::truncated_record, "stream ended inside a record");
        }
        ended_ = true;
    }

    void reset() {
        ensure_idle();
        header_used_ = 0;
        expected_ = 0;
        body_used_ = 0;
        body_.clear();
        failed_ = false;
        ended_ = false;
    }

    [[nodiscard]] bool incomplete() const noexcept { return header_used_ != 0 || expected_ != 0; }
    [[nodiscard]] bool faulted() const noexcept { return failed_; }
    [[nodiscard]] bool ended() const noexcept { return ended_; }

  private:
    struct BusyScope {
        bool& flag;
        explicit BusyScope(bool& value) noexcept : flag(value) { flag = true; }
        ~BusyScope() { flag = false; }
    };
    void ensure_idle() {
        if (busy_) {
            failed_ = true;
            throw RecordError(RecordErrorCode::reentrant_operation, "decoder callback must not reenter decoder");
        }
    }

    std::uint32_t body_limit_;
    std::array<std::byte, record_header_size> header_{};
    std::size_t header_used_{};
    std::uint32_t expected_{};
    std::vector<std::byte> body_;
    std::size_t body_used_{};
    bool busy_{};
    bool failed_{};
    bool ended_{};
};

} // namespace cs2_reader::v1
