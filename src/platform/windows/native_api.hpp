#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstdint>
#include <winternl.h>

namespace cs2::platform::windows {
inline constexpr NTSTATUS status_length_mismatch = static_cast<NTSTATUS>(0xC0000004UL);
inline constexpr NTSTATUS status_buffer_too_small = static_cast<NTSTATUS>(0xC0000023UL);
constexpr bool nt_success(NTSTATUS status) noexcept {
    return status >= 0;
}
} // namespace cs2::platform::windows
