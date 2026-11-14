// Detour of kernelbase!UnhandledExceptionFilter. In the crash
// context the detour only writes into a section the launcher mapped before
// the target resumed, signals one event and waits a bounded time. No
// allocation, no log, no lock, no library load. The real filter is always
// called afterwards and its result is returned unchanged.
#include "tdur/mod/crash_handler.hpp"
#include "tdur/mod/crash_channel.hpp"
#include "tdur/mod/observation_hooks.hpp"
#include "tdur/mod/present_heartbeat.hpp"

#include <detours.h>
#include <intrin.h>
#include <windows.h>

#include <array>
#include <bit>
#include <cstdint>

#pragma intrinsic(_ReturnAddress)

namespace {
namespace crash = tdur::mod::crash;
using Filter = LONG(WINAPI*)(EXCEPTION_POINTERS*);

Filter realFilter = nullptr;
crash::Channel* channel = nullptr;
HANDLE requestEvent = nullptr;
HANDLE doneEvent = nullptr;
HANDLE imageEvent = nullptr;
std::uintptr_t imageBase = 0;
std::uintptr_t imageEnd = 0;

constexpr LONG Claimed = static_cast<LONG>(crash::CrashState::claimed);
constexpr LONG Idle = static_cast<LONG>(crash::CrashState::idle);
constexpr LONG ModTimeout = static_cast<LONG>(crash::CrashState::mod_timeout);

std::uint32_t Address(const void* value) noexcept {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(value));
}

// Keep this frame small (no array, no CRT, no std::): the stack overflow
// case must still reach the real filter.
LONG WINAPI ObserveUnhandledException(EXCEPTION_POINTERS* info) {
    crash::Channel* const ch = channel;
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const EXCEPTION_RECORD* const record =
        info != nullptr ? info->ExceptionRecord : nullptr;
    if (caller >= imageBase && caller < imageEnd) {
        // A deliberate call from the main image (for instance
        // __except(UnhandledExceptionFilter(...))): counted, never dumped.
        InterlockedIncrement(&ch->image_calls);
        ch->image_last_code = record != nullptr ? record->ExceptionCode : 0;
        SetEvent(imageEvent);
        return realFilter(info);
    }
    InterlockedIncrement(&ch->crash_entries);
    ch->last_entry_code = record != nullptr ? record->ExceptionCode : 0;
    // Only the claiming thread owns the exception fields and the real filter
    // result: a later or concurrent entry never overwrites them (review D8).
    const bool claimed =
        InterlockedCompareExchange(&ch->crash_state, Claimed, Idle) == Idle;
    if (claimed) {
        ch->thread_id = GetCurrentThreadId();
        ch->exception_pointers = Address(info);
        ch->caller = static_cast<std::uint32_t>(caller);
        if (record != nullptr) {
            ch->exception_code = record->ExceptionCode;
            ch->exception_flags = record->ExceptionFlags;
            ch->exception_address = Address(record->ExceptionAddress);
            ch->exception_params = record->NumberParameters;
            if (record->NumberParameters >= 2) {
                const auto* const values = record->ExceptionInformation;
                ch->exception_info[0] = static_cast<std::uint32_t>(values[0]);
                ch->exception_info[1] = static_cast<std::uint32_t>(values[1]);
            }
        }
#if defined(_M_IX86)
        if (info != nullptr && info->ContextRecord != nullptr) {
            ch->eip = info->ContextRecord->Eip;
            ch->esp = info->ContextRecord->Esp;
            ch->ebp = info->ContextRecord->Ebp;
        }
#endif
        ch->claimed_tick = GetTickCount64();
        MemoryBarrier();
        SetEvent(requestEvent);
        if (WaitForSingleObject(doneEvent, crash::mod_wait_ms) != WAIT_OBJECT_0)
            InterlockedCompareExchange(&ch->crash_state, ModTimeout, Claimed);
    } else if (ch->crash_state == Claimed) {
        // A second faulting thread while the first one is being dumped.
        WaitForSingleObject(doneEvent, crash::mod_wait_ms);
    }
    const LONG result = realFilter(info);
    if (claimed) {
        ch->real_filter_result = static_cast<std::uint32_t>(result);
        MemoryBarrier();
        InterlockedExchange(&ch->real_filter_done, 1);
    }
    return result;
}

bool ReadEnabled(bool& enabled) noexcept {
    // Same strict rule as TDUR_FRAMETIME_DIAGNOSTICS: absent, or exactly 1.
    std::array<wchar_t, 4> value{};
    SetLastError(ERROR_SUCCESS);
    const auto length = GetEnvironmentVariableW(
        L"TDUR_CRASH_CHANNEL", value.data(), DWORD(value.size()));
    if (length == 0) {
        enabled = false;
        return GetLastError() == ERROR_ENVVAR_NOT_FOUND;
    }
    enabled = length == 1 && value[0] == L'1';
    return enabled;
}

bool Fail(const char* reason, DWORD error) noexcept {
    TdurObservationRecord("crash-handler-install-failed", error, nullptr,
                          reason);
    return false;
}

void* Resolve(const wchar_t* module) noexcept {
    const auto handle = GetModuleHandleW(module);
    return handle != nullptr ? std::bit_cast<void*>(GetProcAddress(
                                   handle, "UnhandledExceptionFilter"))
                             : nullptr;
}
} // namespace

bool TdurInstallCrashHandler() noexcept {
    bool enabled = false;
    if (!ReadEnabled(enabled)) {
        TdurObservationRecord("crash-handler-install-failed",
                              ERROR_INVALID_DATA, nullptr, "invalid-variable");
        return false;
    }
    if (!enabled) {
        TdurObservationRecord("crash-handler-disabled", ERROR_SUCCESS, nullptr,
                              "no-channel");
        return true;
    }
    FILETIME creation{}, exitTime{}, kernelTime{}, userTime{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &kernelTime,
                         &userTime))
        return Fail("process-times", GetLastError());
    const std::uint64_t created =
        (std::uint64_t(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
    const DWORD pid = GetCurrentProcessId();
    std::array<wchar_t, 96> section{}, request{}, done{}, image{};
    if (!crash::format_name(section, pid, created, L"channel") ||
        !crash::format_name(request, pid, created, L"request") ||
        !crash::format_name(done, pid, created, L"done") ||
        !crash::format_name(image, pid, created, L"image"))
        return Fail("name", ERROR_INSUFFICIENT_BUFFER);
    const HANDLE mapping =
        OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, section.data());
    if (mapping == nullptr)
        return Fail("channel-missing", GetLastError());
    auto* const view = static_cast<crash::Channel*>(MapViewOfFile(
        mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, crash::channel_size));
    const DWORD mapError = GetLastError();
    CloseHandle(mapping); // The view keeps the section alive.
    if (view == nullptr)
        return Fail("channel-map", mapError);
    if (!crash::header_valid(*view, pid, created)) {
        UnmapViewOfFile(view);
        return Fail("channel-header", ERROR_INVALID_DATA);
    }
    requestEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, request.data());
    imageEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, image.data());
    doneEvent = OpenEventW(SYNCHRONIZE, FALSE, done.data());
    if (requestEvent == nullptr || imageEvent == nullptr ||
        doneEvent == nullptr) {
        const DWORD error = GetLastError();
        for (const HANDLE handle : {requestEvent, imageEvent, doneEvent})
            if (handle != nullptr)
                CloseHandle(handle);
        requestEvent = imageEvent = doneEvent = nullptr;
        UnmapViewOfFile(view);
        return Fail("channel-events", error);
    }
    const auto base =
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    imageBase = base;
    imageEnd = base + nt->OptionalHeader.SizeOfImage;
    void* const kernelbase = Resolve(L"kernelbase.dll");
    void* const kernel32 = Resolve(L"kernel32.dll");
    if (kernelbase == nullptr)
        return Fail("target-missing", GetLastError());
    TdurObservationRecord("crash-handler-target", ERROR_SUCCESS, nullptr,
                          kernelbase == kernel32 ? "same=1" : "same=0");
    channel = view;
    realFilter = reinterpret_cast<Filter>(kernelbase);
    LONG result = DetourTransactionBegin();
    if (result == NO_ERROR)
        result = DetourUpdateThread(GetCurrentThread());
    if (result == NO_ERROR)
        result =
            DetourAttach(&reinterpret_cast<PVOID&>(realFilter),
                         reinterpret_cast<PVOID>(ObserveUnhandledException));
    if (result != NO_ERROR) {
        DetourTransactionAbort();
    } else {
        result = DetourTransactionCommit();
    }
    if (result != NO_ERROR)
        return Fail("detour", DWORD(result));
    tdur::mod::present_heartbeat.store(&view->present_beats,
                                       std::memory_order_release);
    InterlockedExchange(&view->mod_state, 1);
    TdurObservationRecord("crash-handler-armed", ERROR_SUCCESS);
    return true;
}
