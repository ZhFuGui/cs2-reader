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
struct NativeClientId {
    HANDLE process;
    HANDLE thread;
};
struct ProcessTimes {
    LARGE_INTEGER created, exited, kernel, user;
};
struct BasicProcessInformation {
    NTSTATUS exit_status;
    ULONG padding;
    PVOID peb;
    ULONG_PTR affinity;
    LONG base_priority;
    ULONG reserved;
    ULONG_PTR pid, parent;
};
static_assert(sizeof(BasicProcessInformation) == 48);
inline constexpr NTSTATUS status_length_mismatch = static_cast<NTSTATUS>(0xC0000004UL);
inline constexpr NTSTATUS status_buffer_too_small = static_cast<NTSTATUS>(0xC0000023UL);
inline constexpr NTSTATUS status_access_denied = static_cast<NTSTATUS>(0xC0000022UL);
inline constexpr NTSTATUS status_partial_copy = static_cast<NTSTATUS>(0x8000000DUL);
constexpr bool nt_success(NTSTATUS status) noexcept {
    return status >= 0;
}
} // namespace cs2::platform::windows
// 调用 ntdll 的导出，不硬编码系统调用号，不绕过 ntdll。
extern "C" {
__declspec(dllimport) NTSTATUS NTAPI NtOpenProcess(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES,
                                                   cs2::platform::windows::NativeClientId*);
__declspec(dllimport) NTSTATUS NTAPI NtReadVirtualMemory(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
__declspec(dllimport) NTSTATUS NTAPI NtWriteVirtualMemory(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);
}
