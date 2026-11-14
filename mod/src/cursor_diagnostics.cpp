// Shared user32 attachment for cursor diagnostics and the foreground cursor
// policy. Diagnostics remain optional and metadata-only. The independent
// display policy prevents an unfocused host from retaining the desktop mouse.
#include "tdur/mod/cursor_diagnostics.hpp"
#include "tdur/mod/foreground_cursor.hpp"
#include "tdur/mod/observation_hooks.hpp"
#include "tdur/mod/window_diagnostics.hpp"

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

void ForegroundLog(const char* event, const char* reason) noexcept {
    const DWORD error = GetLastError();
    TdurObservationRecord(event, ERROR_SUCCESS, nullptr, reason);
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

// C5: origin class of a diagnostics caller. The retail site is recognized
// by its verified RVA without any API call (hot path); other callers go
// through a small cache, then the loader. Only handles are compared and only
// the file name leaf is classified; no header, path or name is recorded.
struct OriginCacheEntry {
    std::uintptr_t address{};
    tdur::mod::CallerOrigin origin{};
};
SRWLOCK originLock = SRWLOCK_INIT;
OriginCacheEntry originCache[16]{};
unsigned originNext = 0;

tdur::mod::CallerOrigin ClassifyCaller(const void* caller) noexcept {
    using tdur::mod::CallerOrigin;
    if (!caller)
        return CallerOrigin::unresolved;
    const DWORD error = GetLastError();
    const auto address = reinterpret_cast<std::uintptr_t>(caller);
    const auto main = GetModuleHandleW(nullptr);
    const auto base = reinterpret_cast<std::uintptr_t>(main);
    if (retailVerified && base && address >= base &&
        address - base == 0x0059F87B) {
        SetLastError(error);
        return CallerOrigin::retail_site;
    }
    AcquireSRWLockShared(&originLock);
    for (const auto& entry : originCache) {
        if (entry.address == address) {
            const auto origin = entry.origin;
            ReleaseSRWLockShared(&originLock);
            SetLastError(error);
            return origin;
        }
    }
    ReleaseSRWLockShared(&originLock);
    // The proxy is the module containing this function, obtained once.
    static const HMODULE proxy = [] {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ClassifyCaller), &self);
        return self;
    }();
    auto origin = CallerOrigin::unresolved;
    HMODULE owner = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(caller), &owner) &&
        owner) {
        wchar_t path[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(owner, path, MAX_PATH);
        const wchar_t* leaf = L"";
        if (length != 0 && length < MAX_PATH) {
            leaf = path;
            for (DWORD index = 0; index != length; ++index)
                if (path[index] == L'\\' || path[index] == L'/')
                    leaf = path + index + 1;
        }
        origin = tdur::mod::CallerOriginFromModule(leaf, owner == main,
                                                   proxy && owner == proxy);
    }
    AcquireSRWLockExclusive(&originLock);
    originCache[originNext % std::size(originCache)] = {address, origin};
    ++originNext;
    ReleaseSRWLockExclusive(&originLock);
    SetLastError(error);
    return origin;
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
            ForegroundLog("foreground-retail-hide-clamped", nullptr);
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

// C3: operations on the bound game window, attached only with the opt-in
// diagnostics. Each hook calls its real entry exactly once with the caller's
// arguments and returns its result and last error unchanged. The bridge's
// own calls (display_backend.cpp) are seen too, with the origin "proxy".
// On x86, SetWindowLongPtrW is SetWindowLongW.
auto realSetWindowPos = &SetWindowPos;
auto realMoveWindow = &MoveWindow;
auto realSetWindowLongA = &SetWindowLongA;
auto realSetWindowLongW = &SetWindowLongW;
auto realSetWindowPlacement = &SetWindowPlacement;
auto realShowWindow = &ShowWindow;
auto realShowWindowAsync = &ShowWindowAsync;

using tdur::mod::WindowCall;
using tdur::mod::WindowCallScope;
using tdur::mod::WindowDiagnostics;
using tdur::mod::WindowOp;

WindowDiagnostics* Watched(HWND window) noexcept {
    auto* diagnostics = tdur::mod::ActiveWindowDiagnostics();
    return diagnostics && diagnostics->bound_to(window) ? diagnostics : nullptr;
}

// Whether the requested rectangle equals a monitor's bounds. Missing
// components are read from the current rectangle. Queried only when a line
// can still be written; nothing of the rectangle itself is recorded.
bool FitsMonitor(HWND window, int x, int y, int width, int height,
                 UINT flags) noexcept {
    RECT current{};
    if ((flags & (SWP_NOMOVE | SWP_NOSIZE)) != 0 &&
        !GetWindowRect(window, &current))
        return false;
    if (flags & SWP_NOMOVE) {
        x = current.left;
        y = current.top;
    }
    if (flags & SWP_NOSIZE) {
        width = current.right - current.left;
        height = current.bottom - current.top;
    }
    const RECT target{x, y, x + width, y + height};
    const HMONITOR monitor = MonitorFromRect(&target, MONITOR_DEFAULTTONULL);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    return monitor && GetMonitorInfoW(monitor, &info) &&
           EqualRect(&info.rcMonitor, &target);
}

const char* InsertOrder(HWND after, UINT flags) noexcept {
    if (flags & SWP_NOZORDER)
        return "none";
    if (after == HWND_TOP)
        return "top";
    if (after == HWND_BOTTOM)
        return "bottom";
    if (after == HWND_TOPMOST)
        return "topmost";
    if (after == HWND_NOTOPMOST)
        return "notopmost";
    return "window";
}

BOOL WINAPI HookSetWindowPos(HWND window, HWND after, int x, int y, int width,
                             int height, UINT flags) {
    auto* diagnostics = Watched(window);
    if (!diagnostics)
        return realSetWindowPos(window, after, x, y, width, height, flags);
    const void* caller = _ReturnAddress();
    const DWORD error = GetLastError();
    const auto origin = ClassifyCaller(caller);
    WindowCall call{};
    call.op = WindowOp::set_pos;
    call.width = width;
    call.height = height;
    call.flags = flags;
    call.order = InsertOrder(after, flags);
    if ((flags & (SWP_NOMOVE | SWP_NOSIZE)) != (SWP_NOMOVE | SWP_NOSIZE) &&
        diagnostics->line_available())
        call.fits_monitor = FitsMonitor(window, x, y, width, height, flags);
    diagnostics->api_call(window, call, origin, caller);
    SetLastError(error);
    WindowCallScope scope(WindowOp::set_pos, origin);
    const BOOL result =
        realSetWindowPos(window, after, x, y, width, height, flags);
    if (!result) {
        const DWORD failure = GetLastError();
        diagnostics->api_failed(window, WindowOp::set_pos, failure, origin,
                                caller);
        SetLastError(failure);
    }
    return result;
}

BOOL WINAPI HookMoveWindow(HWND window, int x, int y, int width, int height,
                           BOOL repaint) {
    auto* diagnostics = Watched(window);
    if (!diagnostics)
        return realMoveWindow(window, x, y, width, height, repaint);
    const void* caller = _ReturnAddress();
    const DWORD error = GetLastError();
    const auto origin = ClassifyCaller(caller);
    WindowCall call{};
    call.op = WindowOp::move;
    call.width = width;
    call.height = height;
    if (diagnostics->line_available())
        call.fits_monitor = FitsMonitor(window, x, y, width, height, 0);
    diagnostics->api_call(window, call, origin, caller);
    SetLastError(error);
    WindowCallScope scope(WindowOp::move, origin);
    const BOOL result = realMoveWindow(window, x, y, width, height, repaint);
    if (!result) {
        const DWORD failure = GetLastError();
        diagnostics->api_failed(window, WindowOp::move, failure, origin,
                                caller);
        SetLastError(failure);
    }
    return result;
}

template <class Real>
LONG ObservedSetWindowLong(Real real, HWND window, int index, LONG value,
                           const void* caller) noexcept {
    auto* diagnostics = Watched(window);
    if (!diagnostics || (index != GWL_STYLE && index != GWL_EXSTYLE))
        return real(window, index, value);
    const DWORD error = GetLastError();
    const auto origin = ClassifyCaller(caller);
    const auto op =
        index == GWL_STYLE ? WindowOp::set_style : WindowOp::set_exstyle;
    WindowCall call{};
    call.op = op;
    call.style = value;
    diagnostics->api_call(window, call, origin, caller);
    WindowCallScope scope(op, origin);
    // SetWindowLong leaves the last error untouched on success; a cleared
    // value distinguishes a failure from a previous value of zero.
    SetLastError(ERROR_SUCCESS);
    const LONG result = real(window, index, value);
    const DWORD after = GetLastError();
    if (result == 0 && after != ERROR_SUCCESS) {
        diagnostics->api_failed(window, op, after, origin, caller);
        SetLastError(after);
    } else if (after == ERROR_SUCCESS) {
        SetLastError(error);
    }
    return result;
}

LONG WINAPI HookSetWindowLongA(HWND window, int index, LONG value) {
    return ObservedSetWindowLong(realSetWindowLongA, window, index, value,
                                 _ReturnAddress());
}

LONG WINAPI HookSetWindowLongW(HWND window, int index, LONG value) {
    return ObservedSetWindowLong(realSetWindowLongW, window, index, value,
                                 _ReturnAddress());
}

BOOL WINAPI HookSetWindowPlacement(HWND window,
                                   const WINDOWPLACEMENT* placement) {
    auto* diagnostics = Watched(window);
    if (!diagnostics)
        return realSetWindowPlacement(window, placement);
    const void* caller = _ReturnAddress();
    const DWORD error = GetLastError();
    const auto origin = ClassifyCaller(caller);
    WindowCall call{};
    call.op = WindowOp::placement;
    if (placement) {
        call.command = static_cast<int>(placement->showCmd);
        call.width = placement->rcNormalPosition.right -
                     placement->rcNormalPosition.left;
        call.height = placement->rcNormalPosition.bottom -
                      placement->rcNormalPosition.top;
    }
    diagnostics->api_call(window, call, origin, caller);
    SetLastError(error);
    WindowCallScope scope(WindowOp::placement, origin);
    const BOOL result = realSetWindowPlacement(window, placement);
    if (!result) {
        const DWORD failure = GetLastError();
        diagnostics->api_failed(window, WindowOp::placement, failure, origin,
                                caller);
        SetLastError(failure);
    }
    return result;
}

template <class Real>
BOOL ObservedShow(Real real, WindowOp op, HWND window, int command,
                  const void* caller) noexcept {
    // The result is the previous visibility, not a failure: no failed line.
    auto* diagnostics = Watched(window);
    if (!diagnostics)
        return real(window, command);
    const DWORD error = GetLastError();
    const auto origin = ClassifyCaller(caller);
    WindowCall call{};
    call.op = op;
    call.command = command;
    diagnostics->api_call(window, call, origin, caller);
    SetLastError(error);
    WindowCallScope scope(op, origin);
    return real(window, command);
}

BOOL WINAPI HookShowWindow(HWND window, int command) {
    return ObservedShow(realShowWindow, WindowOp::show, window, command,
                        _ReturnAddress());
}

BOOL WINAPI HookShowWindowAsync(HWND window, int command) {
    return ObservedShow(realShowWindowAsync, WindowOp::show_async, window,
                        command, _ReturnAddress());
}

LONG AttachWindowHooks() noexcept {
    LONG result =
        DetourAttach(&realSetWindowPos,
                     static_cast<decltype(realSetWindowPos)>(HookSetWindowPos));
    if (result == NO_ERROR)
        result =
            DetourAttach(&realMoveWindow,
                         static_cast<decltype(realMoveWindow)>(HookMoveWindow));
    if (result == NO_ERROR)
        result = DetourAttach(
            &realSetWindowLongA,
            static_cast<decltype(realSetWindowLongA)>(HookSetWindowLongA));
    if (result == NO_ERROR)
        result = DetourAttach(
            &realSetWindowLongW,
            static_cast<decltype(realSetWindowLongW)>(HookSetWindowLongW));
    if (result == NO_ERROR)
        result = DetourAttach(&realSetWindowPlacement,
                              static_cast<decltype(realSetWindowPlacement)>(
                                  HookSetWindowPlacement));
    if (result == NO_ERROR)
        result =
            DetourAttach(&realShowWindow,
                         static_cast<decltype(realShowWindow)>(HookShowWindow));
    if (result == NO_ERROR)
        result = DetourAttach(
            &realShowWindowAsync,
            static_cast<decltype(realShowWindowAsync)>(HookShowWindowAsync));
    return result;
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
    // C3: window operations, in the same transaction, diagnostics only.
    if (result == NO_ERROR && observe)
        result = AttachWindowHooks();
    if (result == NO_ERROR)
        result = DetourTransactionCommit();
    else
        DetourTransactionAbort();
    if (result == NO_ERROR) {
        // Built after the commit, so the table holds the trampolines.
        static CursorDiagnostics diagnostics(
            tdur::mod::CursorApiTable{realShowCursor, realSetCursor,
                                      realClipCursor, realReleaseCapture},
            Sink, tdur::mod::CursorLimits{}, ClassifyCaller);
        if (observe) {
            installed.store(&diagnostics, std::memory_order_release);
            tdur::mod::SetActiveCursorDiagnostics(&diagnostics);
            static WindowDiagnostics windows(Sink);
            tdur::mod::SetActiveWindowDiagnostics(&windows);
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
    if (observe) {
        TdurObservationRecord(result == NO_ERROR
                                  ? "cursor-diagnostics-installed"
                                  : "cursor-diagnostics-install-failed",
                              static_cast<DWORD>(result));
        TdurObservationRecord(result == NO_ERROR
                                  ? "window-diagnostics-installed"
                                  : "window-diagnostics-install-failed",
                              static_cast<DWORD>(result));
    }
    if (protectForeground)
        TdurObservationRecord(result == NO_ERROR
                                  ? "foreground-cursor-policy-installed"
                                  : "foreground-cursor-policy-install-failed",
                              static_cast<DWORD>(result));
    return result == NO_ERROR;
}
