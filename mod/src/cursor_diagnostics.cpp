// Shared user32 attachment for cursor diagnostics and the foreground cursor
// policy. Diagnostics remain optional and metadata-only. The independent
// display policy prevents an unfocused host from retaining the desktop mouse.
#include "tdur/mod/cursor_diagnostics.hpp"
#include "tdur/mod/foreground_cursor.hpp"
#include "tdur/mod/observation_hooks.hpp"

#include <detours.h>
#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <iterator>

namespace {

using tdur::mod::CursorDiagnostics;

auto realShowCursor = &ShowCursor;
auto realSetCursor = &SetCursor;
auto realClipCursor = &ClipCursor;
auto realReleaseCapture = &ReleaseCapture;
auto realSetCursorPos = &SetCursorPos;
auto realSetCapture = &SetCapture;
std::atomic<CursorDiagnostics*> installed{nullptr};
std::atomic<tdur::mod::ForegroundCursor*> foregroundPolicy{nullptr};
std::atomic<unsigned> blockedLines{0};
std::atomic<bool> hideClampLogged{false};
bool retailVerified = false;

void ForegroundLog(const char* event) noexcept {
    const DWORD error = GetLastError();
    TdurObservationRecord(event, ERROR_SUCCESS);
    SetLastError(error);
}

void Blocked(const char* operation) noexcept {
    auto line = blockedLines.load(std::memory_order_relaxed);
    while (line < 128 && !blockedLines.compare_exchange_weak(
                             line, line + 1, std::memory_order_relaxed)) {
    }
    if (line < 128)
        TdurObservationRecord("foreground-cursor-blocked", ERROR_ACCESS_DENIED,
                              nullptr, operation);
}

bool RepeatedRetailHide(const void* caller) noexcept {
    // Official MC1.66A A/B logs: the same main-image return RVA produced
    // 2,502 unbalanced hides before Alt+Tab, then continued while inactive.
    // Do not change nesting semantics for any other caller or library.
    if (!retailVerified)
        return false;
    const auto base =
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto address = reinterpret_cast<std::uintptr_t>(caller);
    return base && address >= base && address - base == 0x0059F87B;
}

bool Background() noexcept {
    const auto* policy = foregroundPolicy.load(std::memory_order_acquire);
    return policy && policy->background();
}

void Sink(const tdur::mod::CursorRecord& record) noexcept {
    TdurObservationRecord(record.event, record.error, nullptr, record.reason,
                          record.caller);
}

// Until the instance is published after the commit, hooks pass straight to
// the trampolines, so no call is lost or duplicated.
int WINAPI HookShowCursor(BOOL show) {
    const void* caller = _ReturnAddress();
    const DWORD error = GetLastError();
    const bool background = Background();
    SetLastError(error);
    if (background) {
        Blocked(show ? "show" : "hide");
        SetLastError(error);
        return tdur::mod::ForegroundCursor::background_show_result(show);
    }
    auto* policy = foregroundPolicy.load(std::memory_order_acquire);
    int previous{};
    if (!show && policy && RepeatedRetailHide(caller) &&
        policy->skip_repeated_retail_hide(previous)) {
        if (!hideClampLogged.exchange(true, std::memory_order_relaxed))
            ForegroundLog("foreground-retail-hide-clamped");
        SetLastError(error);
        return previous;
    }
    SetLastError(error);
    auto* diagnostics = installed.load(std::memory_order_acquire);
    const auto result = diagnostics ? diagnostics->show_cursor(show, caller)
                                    : realShowCursor(show);
    const DWORD returnedError = GetLastError();
    if (policy)
        policy->observed_show(result);
    SetLastError(returnedError);
    return result;
}

HCURSOR WINAPI HookSetCursor(HCURSOR cursor) {
    const DWORD error = GetLastError();
    if (Background()) {
        Blocked("set-cursor");
        const auto previous = GetCursor();
        SetLastError(error);
        return previous;
    }
    SetLastError(error);
    const void* caller = _ReturnAddress();
    auto* diagnostics = installed.load(std::memory_order_acquire);
    return diagnostics ? diagnostics->set_cursor(cursor, caller)
                       : realSetCursor(cursor);
}

BOOL WINAPI HookClipCursor(const RECT* rect) {
    const DWORD error = GetLastError();
    if (rect && Background()) {
        Blocked("clip");
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    SetLastError(error);
    const void* caller = _ReturnAddress();
    auto* diagnostics = installed.load(std::memory_order_acquire);
    const auto result = diagnostics ? diagnostics->clip_cursor(rect, caller)
                                    : realClipCursor(rect);
    const DWORD returnedError = GetLastError();
    if (auto* policy = foregroundPolicy.load(std::memory_order_acquire))
        policy->clipped(rect, result);
    SetLastError(returnedError);
    return result;
}

BOOL WINAPI HookSetCursorPos(int x, int y) {
    const DWORD error = GetLastError();
    if (Background()) {
        Blocked("warp");
        SetLastError(ERROR_ACCESS_DENIED);
        return FALSE;
    }
    SetLastError(error);
    return realSetCursorPos(x, y);
}

HWND WINAPI HookSetCapture(HWND window) {
    const DWORD error = GetLastError();
    if (Background()) {
        Blocked("capture");
        SetLastError(ERROR_ACCESS_DENIED);
        return nullptr;
    }
    SetLastError(error);
    return realSetCapture(window);
}

BOOL WINAPI HookReleaseCapture() {
    const void* caller = _ReturnAddress();
    auto* diagnostics = installed.load(std::memory_order_acquire);
    return diagnostics ? diagnostics->release_capture(caller)
                       : realReleaseCapture();
}

} // namespace

bool TdurInstallCursorDiagnostics() noexcept {
    wchar_t value[4]{};
    SetLastError(ERROR_SUCCESS);
    const DWORD length =
        GetEnvironmentVariableW(L"TDUR_CURSOR_DIAGNOSTICS", value,
                                static_cast<DWORD>(std::size(value)));
    const auto setting =
        tdur::mod::ParseCursorDiagnosticsSetting(value, length, GetLastError());
    if (setting == tdur::mod::CursorDiagnosticsSetting::invalid) {
        TdurObservationRecord("cursor-diagnostics-invalid-configuration",
                              ERROR_INVALID_DATA);
        return false;
    }
    // A foreground cursor policy is part of the requested display bridge,
    // independent of the optional metadata-only diagnostics.
    wchar_t display[4]{};
    const auto displayLength = GetEnvironmentVariableW(
        L"TDUR_D3D12_DISPLAY", display, static_cast<DWORD>(std::size(display)));
    const bool protectForeground = displayLength == 1 && display[0] == L'1';
    const bool observe =
        setting == tdur::mod::CursorDiagnosticsSetting::enabled;
    if (!observe && !protectForeground)
        return true;
    wchar_t verified[4]{};
    const auto verifiedLength =
        GetEnvironmentVariableW(L"TDUR_RETAIL_EXECUTABLE_VERIFIED", verified,
                                static_cast<DWORD>(std::size(verified)));
    retailVerified = verifiedLength == 1 && verified[0] == L'1';
    LONG result = DetourTransactionBegin();
    if (result == NO_ERROR)
        result = DetourUpdateThread(GetCurrentThread());
    if (result == NO_ERROR)
        result =
            DetourAttach(&realShowCursor,
                         static_cast<decltype(realShowCursor)>(HookShowCursor));
    if (result == NO_ERROR)
        result =
            DetourAttach(&realSetCursor,
                         static_cast<decltype(realSetCursor)>(HookSetCursor));
    if (result == NO_ERROR)
        result =
            DetourAttach(&realClipCursor,
                         static_cast<decltype(realClipCursor)>(HookClipCursor));
    if (result == NO_ERROR)
        result = DetourAttach(
            &realReleaseCapture,
            static_cast<decltype(realReleaseCapture)>(HookReleaseCapture));
    if (result == NO_ERROR && protectForeground)
        result = DetourAttach(
            &realSetCursorPos,
            static_cast<decltype(realSetCursorPos)>(HookSetCursorPos));
    if (result == NO_ERROR && protectForeground)
        result =
            DetourAttach(&realSetCapture,
                         static_cast<decltype(realSetCapture)>(HookSetCapture));
    if (result == NO_ERROR)
        result = DetourTransactionCommit();
    else
        DetourTransactionAbort();
    if (result == NO_ERROR) {
        // Built after the commit, so the table holds the trampolines.
        static CursorDiagnostics diagnostics(
            tdur::mod::CursorApiTable{realShowCursor, realSetCursor,
                                      realClipCursor, realReleaseCapture},
            Sink);
        if (observe) {
            installed.store(&diagnostics, std::memory_order_release);
            tdur::mod::SetActiveCursorDiagnostics(&diagnostics);
        }
        if (protectForeground) {
            static tdur::mod::ForegroundCursor policy(
                {GetCurrentThreadId, GetForegroundWindow, IsChild, GetCapture,
                 realReleaseCapture, realClipCursor, GetClipCursor,
                 realShowCursor, realSetCursor},
                ForegroundLog);
            foregroundPolicy.store(&policy, std::memory_order_release);
            tdur::mod::SetActiveForegroundCursor(&policy);
        }
    }
    if (observe)
        TdurObservationRecord(result == NO_ERROR
                                  ? "cursor-diagnostics-installed"
                                  : "cursor-diagnostics-install-failed",
                              static_cast<DWORD>(result));
    if (protectForeground)
        TdurObservationRecord(result == NO_ERROR
                                  ? "foreground-cursor-policy-installed"
                                  : "foreground-cursor-policy-install-failed",
                              static_cast<DWORD>(result));
    return result == NO_ERROR;
}
