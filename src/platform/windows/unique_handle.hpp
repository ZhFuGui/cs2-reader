#pragma once
#include "platform/windows/native_api.hpp"
#include <utility>
namespace cs2::platform::windows {
class UniqueHandle {
  public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
    ~UniqueHandle() { reset(); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    UniqueHandle(UniqueHandle&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }
    HANDLE get() const noexcept { return handle_; }
    void reset() noexcept {
        if (handle_ && handle_ != INVALID_HANDLE_VALUE)
            ::NtClose(handle_);
        handle_ = nullptr;
    }

  private:
    HANDLE handle_ = nullptr;
};
} // namespace cs2::platform::windows
