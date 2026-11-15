#include "tdur/game/fixed_step.hpp"
#include "tdur/mod/display_backend.hpp"

#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using namespace tdur::mod;
using Microsoft::WRL::ComPtr;
unsigned checks = 0;

void Require(bool condition, const char* message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}

void Checked(HRESULT result, const char* message) {
    if (FAILED(result))
        std::cerr << message << " HRESULT=0x" << std::hex
                  << static_cast<unsigned long>(result) << std::dec << "\n";
    Require(SUCCEEDED(result), message);
}

std::uint64_t window_position_messages = 0;

LRESULT CALLBACK WitnessWindowProcedure(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam) {
    if (message == WM_WINDOWPOSCHANGING)
        ++window_position_messages;
    return DefWindowProcW(window, message, wparam, lparam);
}

struct Window {
    HWND value = nullptr;
    Window() {
        WNDCLASSW klass{};
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpfnWndProc = WitnessWindowProcedure;
        klass.lpszClassName = L"TDUR.DisplayBackend.Witness.v1";
        Require(RegisterClassW(&klass) != 0 ||
                    GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
                "register witness window");
        value = CreateWindowExW(0, klass.lpszClassName,
                                L"TDU reconstruction - display API witness",
                                WS_OVERLAPPEDWINDOW, 80, 100, 480, 320, nullptr,
                                nullptr, klass.hInstance, nullptr);
        Require(value != nullptr, "create hidden witness window");
    }
    ~Window() {
        if (value)
            DestroyWindow(value);
    }
};

// Witness shared by the window-operation and constraint tests: invisible
// even when shown (layered, alpha 0), traversed by the mouse and never
// activated. Only SW_SHOWMINNOACTIVE and SW_SHOWNOACTIVATE are ever used.
struct ConstraintTrace {
    std::vector<bool> internal_changing; // per WM_WINDOWPOSCHANGING
    std::vector<bool> minimized_changing;
    std::vector<UINT> changing_flags; // WINDOWPOS flags as received
    std::vector<SIZE> changed; // window size after each WM_WINDOWPOSCHANGED
    std::vector<SIZE> sizes;   // WM_SIZE client size
} trace;

// C2: the backend whose constraint the witness procedure applies, read
// through its owner like display_hooks.cpp reads ctx->backend (null while
// the backend is being destroyed), and the backend's correction lines.
std::unique_ptr<DisplayBackend>* constrained = nullptr;
std::vector<std::string> backendLines;
void CollectBackendLine(const char* event, void*) noexcept {
    backendLines.emplace_back(event);
}
std::size_t LinesStartingWith(const char* prefix) {
    std::size_t count = 0;
    for (const auto& line : backendLines)
        if (line.starts_with(prefix))
            ++count;
    return count;
}

// The host of the witness, like a game procedure behind the bridge's
// subclass: optionally it redemands 320x240 at each other restored WM_SIZE
// (a resize callback), or rewrites any size change it is asked to make.
struct HostBehavior {
    bool react_to_size = false;
    bool rewrite_size = false;
    unsigned reactions = 0;
    unsigned reactions_during_bridge = 0;
} host;

LRESULT HostProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_WINDOWPOSCHANGING && host.rewrite_size) {
        auto* position = reinterpret_cast<WINDOWPOS*>(lparam);
        if ((position->flags & SWP_NOSIZE) == 0 &&
            (position->cx != 320 || position->cy != 240) &&
            position->x > -32000) {
            position->cx = 320;
            position->cy = 240;
        }
    }
    const auto result = DefWindowProcW(window, message, wparam, lparam);
    // Once per size: a framed 320x240 window has a smaller client area.
    RECT outer{};
    if (message == WM_SIZE && host.react_to_size && wparam == SIZE_RESTORED &&
        (LOWORD(lparam) != 320 || HIWORD(lparam) != 240) &&
        GetWindowRect(window, &outer) &&
        (outer.right - outer.left != 320 || outer.bottom - outer.top != 240)) {
        ++host.reactions;
        // Still inside the outer message's host handler: never internal.
        if (DisplayWindowOperationActive())
            ++host.reactions_during_bridge;
        SetWindowPos(window, nullptr, 0, 0, 320, 240,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return result;
}

LRESULT CALLBACK ConstraintWindowProcedure(HWND window, UINT message,
                                           WPARAM wparam, LPARAM lparam) {
    if (message == WM_WINDOWPOSCHANGING) {
        trace.internal_changing.push_back(DisplayWindowOperationActive());
        trace.minimized_changing.push_back(
            (GetWindowLongPtrW(window, GWL_STYLE) & WS_MINIMIZE) != 0);
        trace.changing_flags.push_back(
            reinterpret_cast<const WINDOWPOS*>(lparam)->flags);
    }
    // As display_hooks.cpp wraps DefSubclassProc.
    LRESULT result{};
    {
        DisplayHostHandlerScope scope;
        result = HostProcedure(window, message, wparam, lparam);
    }
    if (constrained && *constrained &&
        (message == WM_WINDOWPOSCHANGING || message == WM_STYLECHANGING))
        (void)(*constrained)->constrain_window_message(message, wparam, lparam);
    if (message == WM_WINDOWPOSCHANGED) {
        RECT rectangle{};
        GetWindowRect(window, &rectangle);
        trace.changed.push_back({rectangle.right - rectangle.left,
                                 rectangle.bottom - rectangle.top});
    } else if (message == WM_SIZE) {
        trace.sizes.push_back({static_cast<LONG>(LOWORD(lparam)),
                               static_cast<LONG>(HIWORD(lparam))});
    }
    return result;
}

struct ConstraintWindow {
    HWND value = nullptr;
    ConstraintWindow() {
        WNDCLASSW klass{};
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpfnWndProc = ConstraintWindowProcedure;
        klass.lpszClassName = L"TDUR.DisplayBackend.Constraint.v1";
        Require(RegisterClassW(&klass) != 0 ||
                    GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
                "register constraint witness window");
        value = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT |
                                    WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                                klass.lpszClassName,
                                L"TDU reconstruction - constraint witness",
                                WS_OVERLAPPEDWINDOW, 80, 100, 480, 320, nullptr,
                                nullptr, klass.hInstance, nullptr);
        Require(value != nullptr, "create hidden constraint witness");
        Require(SetLayeredWindowAttributes(value, 0, 0, LWA_ALPHA) != FALSE,
                "constraint witness fully transparent");
    }
    ~ConstraintWindow() {
        if (value)
            DestroyWindow(value);
    }
};

// Every WM_WINDOWPOSCHANGING from index from is the bridge's own, except
// the frame-only SetWindowPos (SWP_NOSIZE | SWP_NOMOVE) that DefWindowProc
// makes while handling WM_STYLECHANGED: a call from inside the host's
// handler, measured 0x1001837 during set_mode(windowed), never constrained.
bool BridgeOwnedFrom(std::size_t from) {
    constexpr UINT frameOnly = SWP_NOSIZE | SWP_NOMOVE;
    for (std::size_t index = from; index < trace.internal_changing.size();
         ++index)
        if (!trace.internal_changing[index] &&
            (trace.changing_flags[index] & frameOnly) != frameOnly)
            return false;
    return from < trace.internal_changing.size();
}

bool SameRectangle(RECT a, RECT b) {
    return a.left == b.left && a.top == b.top && a.right == b.right &&
           a.bottom == b.bottom;
}

bool SawSize(const std::vector<SIZE>& sizes, LONG width, LONG height) {
    for (const auto& size : sizes)
        if (size.cx == width && size.cy == height)
            return true;
    return false;
}

RECT Rectangle(HWND window) {
    RECT rectangle{};
    Require(GetWindowRect(window, &rectangle) != FALSE,
            "read witness rectangle");
    return rectangle;
}

bool Sized(RECT rectangle, LONG width, LONG height) {
    return rectangle.right - rectangle.left == width &&
           rectangle.bottom - rectangle.top == height;
}

void SwitchAndGuardPolicyTest() {
    Require(ParseSwitchSetting(nullptr, 0, ERROR_ENVVAR_NOT_FOUND) ==
                SwitchSetting::absent,
            "switch: absent");
    Require(ParseSwitchSetting(L"1", 1, ERROR_SUCCESS) ==
                SwitchSetting::enabled,
            "switch: 1 enables");
    Require(ParseSwitchSetting(L"0", 1, ERROR_SUCCESS) ==
                SwitchSetting::disabled,
            "switch: 0 disables");
    Require(ParseSwitchSetting(L"2", 1, ERROR_SUCCESS) ==
                    SwitchSetting::invalid &&
                ParseSwitchSetting(L"10", 2, ERROR_SUCCESS) ==
                    SwitchSetting::invalid &&
                ParseSwitchSetting(L"", 0, ERROR_SUCCESS) ==
                    SwitchSetting::invalid &&
                ParseSwitchSetting(nullptr, 0, ERROR_INVALID_PARAMETER) ==
                    SwitchSetting::invalid,
            "switch: anything else is invalid");

    using Guard = BorderlessCorrectionGuard;
    {
        Guard guard;
        for (const std::uint64_t now : {0ULL, 300ULL, 600ULL, 900ULL})
            Require(guard.record(Guard::Kind::constrained, now) ==
                            Guard::Verdict::apply &&
                        !guard.opened_episode(),
                    "guard: four constraints in 900 ms are applied");
        Require(guard.record(Guard::Kind::constrained, 950) ==
                        Guard::Verdict::apply_storm &&
                    guard.opened_episode() && guard.counters().storms == 1,
                "guard: a fifth constraint opens the episode, applied");
        Require(guard.record(Guard::Kind::constrained, 960) ==
                        Guard::Verdict::apply &&
                    guard.counters().storms == 1,
                "guard: constraints inside the episode stay applied");
        Require(guard.record(Guard::Kind::repaired, 970) ==
                        Guard::Verdict::suspend &&
                    guard.counters().suspended == 1,
                "guard: a repair inside the episode is suspended");
        Require(guard.counters().constrained == 6 &&
                    guard.counters().repaired == 0,
                "guard: counters of applied corrections");
    }
    {
        Guard guard;
        for (const std::uint64_t now : {0ULL, 100ULL, 200ULL, 300ULL})
            Require(guard.record(Guard::Kind::repaired, now) ==
                        Guard::Verdict::apply,
                    "guard: four repairs are applied");
        Require(guard.record(Guard::Kind::repaired, 400) ==
                        Guard::Verdict::suspend &&
                    guard.opened_episode(),
                "guard: the repair opening the episode is suspended");
        Require(guard.record(Guard::Kind::repaired, 900) ==
                    Guard::Verdict::suspend,
                "guard: a repair 500 ms after the opening is suspended");
        Require(guard.record(Guard::Kind::repaired, 1500) ==
                        Guard::Verdict::apply &&
                    !guard.opened_episode(),
                "guard: a repair 1,100 ms after the opening is applied");
        Require(guard.counters().repaired == 5 &&
                    guard.counters().suspended == 2 &&
                    guard.counters().storms == 1,
                "guard: repaired, suspended and storm counters");
    }
    {
        Guard guard;
        for (const std::uint64_t now : {0ULL, 300ULL, 600ULL, 900ULL, 1200ULL})
            Require(guard.record(Guard::Kind::repaired, now) ==
                        Guard::Verdict::apply,
                    "guard: five corrections over 1,200 ms open nothing");
        Require(guard.counters().storms == 0, "guard: no spread-out storm");
        Require(guard.record(Guard::Kind::migration, 1210) ==
                        Guard::Verdict::apply &&
                    guard.counters().migrations == 1 &&
                    guard.counters().storms == 0,
                "guard: migrations are counted outside the frequency");
    }
    {
        Guard guard;
        unsigned individual = 0;
        unsigned storms = 0;
        while (guard.take_individual_line())
            ++individual;
        while (guard.take_storm_line())
            ++storms;
        Require(individual == 16 && storms == 16,
                "guard: 16 individual and 16 storm lines");
        Require(!guard.summary_due(100000) && !guard.final_summary_due(),
                "guard: no summary without any correction");
        (void)guard.record(Guard::Kind::constrained, 1000);
        Require(!guard.summary_due(5999) && guard.summary_due(6000),
                "guard: first summary 5 s after the first correction");
        Require(!guard.summary_due(20000),
                "guard: no summary without a counter change");
        std::vector<std::uint64_t> at{6000};
        unsigned notices = 0;
        bool noticeAfter32 = false;
        std::uint64_t now = 6000;
        for (unsigned round = 0; round != 40; ++round) {
            now += 5000;
            (void)guard.record(Guard::Kind::constrained, now);
            if (guard.summary_due(now))
                at.push_back(now);
            if (guard.take_sparse_notice()) {
                ++notices;
                noticeAfter32 = at.size() == 32;
            }
        }
        Require(at.size() == 34 && at[31] == 161000 && at[32] == 171000 &&
                    at[33] == 191000,
                "guard: 32 summaries 5 s apart, then 10 s, then 20 s");
        Require(notices == 1 && noticeAfter32,
                "guard: one sparse notice, right after the 32nd summary");
        Require(guard.final_summary_due() && !guard.final_summary_due(),
                "guard: final summary outside the budget, once");
        Guard closing;
        (void)closing.record(Guard::Kind::repaired, 0);
        Require(closing.final_summary_due() && !closing.final_summary_due(),
                "guard: one final summary when a counter changed");
        Guard skipping;
        skipping.record_skipped();
        Require(skipping.counters().skipped == 1 &&
                    !skipping.summary_due(100000) &&
                    skipping.final_summary_due(),
                "guard: an abstention is counted and summarized");
    }
    {
        // A cause that never stops: one repair every 16 ms for 10 minutes,
        // the shape of maintainBorderless under a host that keeps shrinking.
        Guard guard;
        std::uint64_t lastApplied = 0;
        std::uint64_t longestGap = 0;
        std::uint64_t lastSummary = 0;
        std::uint64_t longestSilence = 0;
        unsigned summaries = 0;
        for (std::uint64_t now = 0; now <= 600000; now += 16) {
            if (guard.record(Guard::Kind::repaired, now) ==
                Guard::Verdict::apply) {
                longestGap = std::max(longestGap, now - lastApplied);
                lastApplied = now;
            }
            if (guard.summary_due(now)) {
                ++summaries;
                longestSilence = std::max(longestSilence, now - lastSummary);
                lastSummary = now;
            }
        }
        const auto& n = guard.counters();
        Require(longestGap <= 1000 + 2 * 16,
                "guard: a lasting cause still gets a repair after each "
                "suspension");
        Require(n.repaired >= 2000 && n.storms >= 500 &&
                    n.repaired >= 4 * n.storms &&
                    n.repaired <= 4 * n.storms + 4 &&
                    600000 - lastApplied <= 1032,
                "guard: four repairs per episode under a lasting cause");
        Require(summaries == 37 && longestSilence <= 160000 + 16 * 37 &&
                    guard.final_summary_due(),
                "guard: summaries never stop under a lasting cause, final "
                "one kept");
    }
}

void PolicyTest() {
    DisplayPreferences p;
    Require(p.mode == WindowMode::borderless, "default borderless");
    Require(p.maximum_reference_quality,
            "reference max capabilities requested");
    Require(EffectiveFrameCap(p, 60000.0 / 1001.0) == 60000.0 / 1001.0,
            "fractional native cap retained");
    p.cap = FrameCapMode::native_minus_three;
    Require(EffectiveFrameCap(p, 160) == 157, "optional margin");
    Require(EffectiveFrameCap(p, 2) == 1, "margin lower bound");
    p.cap = FrameCapMode::unlimited;
    Require(EffectiveFrameCap(p, 0) == 0, "unlimited independent of monitor");
    p.cap = FrameCapMode::fixed;
    p.fixed_fps = 144;
    Require(EffectiveFrameCap(p, 60) == 144, "explicit cap");
    for (const double invalid :
         {0.0, -1.0, 1001.0, std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::quiet_NaN()}) {
        p.fixed_fps = invalid;
        bool rejected = false;
        try {
            (void)EffectiveFrameCap(p, 160);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        Require(rejected, "invalid cap rejected");
    }
    for (unsigned bits = 0; bits < 16; ++bits) {
        const bool exclusive = (bits & 1) != 0;
        const bool support = (bits & 2) != 0;
        const bool allow = (bits & 4) != 0;
        const bool vsync = (bits & 8) != 0;
        const auto policy =
            SelectPresentationPolicy(exclusive, support, allow, vsync);
        Require(policy.sync_interval == (vsync ? 1U : 0U), "VSync interval");
        const bool eligible = !exclusive && support && allow && !vsync;
        Require(policy.vrr_eligible == eligible, "conditional VRR eligibility");
        Require(policy.present_flags ==
                    (eligible ? DXGI_PRESENT_ALLOW_TEARING : 0U),
                "tearing flags restricted to supported windowed sync0");
    }
}

void WindowTest() {
    Window window;
    RECT original{};
    Require(GetWindowRect(window.value, &original),
            "original window rectangle");
    const auto originalStyle = GetWindowLongPtrW(window.value, GWL_STYLE);
    const auto originalForeground = GetForegroundWindow();
    const auto monitors = DisplayBackend::monitors();
    Require(!monitors.empty(), "at least one monitor");
    {
        DisplayPreferences preferences;
        DisplayBackend display(window.value, preferences);
        Require(display.render_window() != window.value,
                "separate render sink HWND");
        Require(!IsWindowVisible(display.render_window()),
                "sink remains hidden");
        for (const auto& monitor : monitors) {
            display.move_to_monitor(monitor.handle);
            const auto state = display.state();
            Require(state.monitor.handle == monitor.handle,
                    "actual monitor migration");
            Require(state.monitor.refresh_hz() > 1,
                    "monitor refresh available");
            Require(state.effective_cap_hz == state.monitor.refresh_hz(),
                    "native cap follows actual monitor after migration");
            Require(state.requested_mode == WindowMode::borderless &&
                        !state.exclusive_active,
                    "borderless distinct from exclusive");
            RECT rectangle{};
            Require(GetWindowRect(window.value, &rectangle),
                    "borderless rectangle");
            Require(SameRectangle(rectangle, monitor.bounds),
                    "borderless covers monitor pixels");
            Require((GetWindowLongPtrW(window.value, GWL_STYLE) &
                     (WS_CAPTION | WS_THICKFRAME)) == 0,
                    "borderless has no decoration");
            Require(!IsWindowVisible(window.value),
                    "mode change does not show hidden window");
            std::wcout << L"monitor=" << monitor.gdi_name << L" refresh="
                       << monitor.refresh_numerator << L"/"
                       << monitor.refresh_denominator << L" rational="
                       << monitor.rational_refresh_observed
                       << L" clone_ambiguous="
                       << monitor.cloned_source_ambiguous << L"\n";
            display.set_mode(WindowMode::windowed);
            Require((GetWindowLongPtrW(window.value, GWL_STYLE) &
                     WS_OVERLAPPEDWINDOW) == WS_OVERLAPPEDWINDOW,
                    "windowed decoration restored");
            Require(!display.state().exclusive_active, "windowed DXGI state");
            display.set_mode(WindowMode::borderless);
        }
        // Display changes mark the cached monitor description stale, on the
        // owner thread only; everything else is ignored.
        Require(
            display.note_display_change(WM_DISPLAYCHANGE, 32) &&
                display.note_display_change(WM_DPICHANGED, 0) &&
                display.note_display_change(WM_SETTINGCHANGE, SPI_SETWORKAREA),
            "display, DPI and work-area changes mark the monitor stale");
        Require(!display.note_display_change(WM_SETTINGCHANGE, 0) &&
                    !display.note_display_change(WM_SIZE, 0) &&
                    !display.note_display_change(WM_WINDOWPOSCHANGING, 0),
                "other messages leave the monitor description alone");
        bool foreignMarked = true;
        std::thread foreign([&] {
            foreignMarked = display.note_display_change(WM_DISPLAYCHANGE, 32);
        });
        foreign.join();
        Require(!foreignMarked, "another thread never marks the monitor");
        const auto reread = display.state();
        Require(reread.monitor.handle != nullptr &&
                    reread.monitor.refresh_hz() > 1,
                "a stale monitor description is reread on demand");
        const auto described = display.state().monitor_descriptions;
        Require(display.state().monitor_descriptions == described,
                "no monitor query without a reason");
        Require(display.note_display_change(WM_ACTIVATEAPP, TRUE) &&
                    !display.note_display_change(WM_ACTIVATEAPP, FALSE),
                "activation is the safety net, deactivation is not");
        Require(display.state().monitor_descriptions == described + 1,
                "one query per marked change");
        Require(GetForegroundWindow() == originalForeground,
                "mode changes do not steal focus");
        Require(!display.state().window_has_foreground,
                "actual witness has no focus");
        Require(!display.state().process_has_foreground,
                "witness process is not foreground");
        bool exclusiveRejected = false;
        try {
            display.set_mode(WindowMode::exclusive);
        } catch (const DisplayError&) {
            exclusiveRejected = true;
        }
        Require(exclusiveRejected,
                "exclusive cannot be faked without a swapchain");
        Require(!display.state().exclusive_active,
                "exclusive failure remains explicit");
    }
    Require(GetWindowLongPtrW(window.value, GWL_STYLE) == originalStyle,
            "original style restored by destructor");
    RECT restored{};
    Require(GetWindowRect(window.value, &restored), "restored rectangle");
    Require(SameRectangle(restored, original), "original placement restored");
    Require(GetForegroundWindow() == originalForeground,
            "restoration does not steal focus");
}

void BorderlessRecoveryTest() {
    Window window;
    const auto foreground = GetForegroundWindow();
    DisplayPreferences preferences;
    preferences.cap = FrameCapMode::unlimited;
    DisplayBackend display(window.value, preferences);
    const auto monitor = display.state().monitor;
    // Simulate a host resizing/restyling its HWND after CreateDevice or Reset.
    SetWindowLongPtrW(window.value, GWL_STYLE, WS_OVERLAPPEDWINDOW);
    Require(SetWindowPos(window.value, nullptr, monitor.bounds.left + 10,
                         monitor.bounds.top + 10, 320, 240,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED),
            "resize owned borderless host");
    display.wait_for_frame();
    RECT actual{};
    Require(GetWindowRect(window.value, &actual), "read recovered rectangle");
    Require(SameRectangle(actual, monitor.bounds),
            "borderless recovers monitor bounds after host resize");
    Require((GetWindowLongPtrW(window.value, GWL_STYLE) &
             (WS_POPUP | WS_CAPTION | WS_THICKFRAME)) == WS_POPUP,
            "borderless recovers undecorated style");
    Require(!IsWindowVisible(window.value) &&
                GetForegroundWindow() == foreground,
            "recovery neither shows nor focuses owned hidden host");
    const auto messages = window_position_messages;
    for (unsigned index = 0; index != 10; ++index)
        display.wait_for_frame();
    Require(window_position_messages == messages,
            "stable borderless frames do not reposition the window");

    display.set_mode(WindowMode::windowed);
    Require(SetWindowPos(window.value, nullptr, monitor.bounds.left + 30,
                         monitor.bounds.top + 30, 360, 260,
                         SWP_NOZORDER | SWP_NOACTIVATE),
            "resize owned windowed host");
    RECT resized{};
    Require(GetWindowRect(window.value, &resized), "read windowed resize");
    const auto windowedMessages = window_position_messages;
    display.wait_for_frame();
    Require(GetWindowRect(window.value, &actual) &&
                SameRectangle(actual, resized) &&
                window_position_messages == windowedMessages,
            "windowed mode preserves a host resize");
}

void WindowOperationScopeTest() {
    ConstraintWindow window;
    const auto foreground = GetForegroundWindow();
    const auto monitors = DisplayBackend::monitors();
    trace = {};
    std::size_t destruction = 0;
    {
        DisplayPreferences preferences;
        preferences.cap = FrameCapMode::unlimited;
        DisplayBackend display(window.value, preferences);
        display.set_mode(WindowMode::windowed);
        display.set_mode(WindowMode::borderless);
        for (const auto& monitor : monitors)
            display.move_to_monitor(monitor.handle);
        Require(BridgeOwnedFrom(0),
                "bridge style, mode and monitor changes are internal");
        Require(!DisplayWindowOperationActive(),
                "no window operation outside the bridge");
        const auto own = trace.internal_changing.size();
        Require(SetWindowPos(window.value, nullptr, 0, 0, 320, 240,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE),
                "foreign resize of the witness");
        Require(trace.internal_changing.size() == own + 1 &&
                    !trace.internal_changing.back(),
                "a foreign SetWindowPos is not internal");
        // The next frame repairs the foreign 320x240: the bridge's own.
        const auto beforeRepair = trace.internal_changing.size();
        display.wait_for_frame();
        Require(display.corrections().repaired == 1 &&
                    trace.internal_changing.size() > beforeRepair &&
                    BridgeOwnedFrom(beforeRepair),
                "a borderless repair is internal");
        // A call the host makes from its WM_SIZE handler while the bridge
        // changes mode is the host's, at every depth (C3 internal=0).
        host = {};
        host.react_to_size = true;
        const auto beforeReaction = trace.internal_changing.size();
        display.set_mode(WindowMode::windowed);
        display.set_mode(WindowMode::borderless);
        host.react_to_size = false;
        std::size_t foreignMessages = 0;
        for (std::size_t index = beforeReaction;
             index < trace.internal_changing.size(); ++index)
            if (!trace.internal_changing[index] &&
                (trace.changing_flags[index] & SWP_NOSIZE) == 0)
                ++foreignMessages;
        std::cout << "scope_host_reactions=" << host.reactions
                  << " foreign_changing=" << foreignMessages << "\n";
        Require(host.reactions >= 2 && host.reactions_during_bridge == 0 &&
                    foreignMessages == host.reactions,
                "host calls nested in a bridge operation are not internal");
        destruction = trace.internal_changing.size();
    }
    Require(BridgeOwnedFrom(destruction),
            "restoration at destruction is internal");
    Require(!DisplayWindowOperationActive(), "scope closed after destruction");
    Require(!IsWindowVisible(window.value) &&
                GetForegroundWindow() == foreground,
            "scope test neither shows nor focuses the witness");
}

DisplayPreferences ConstraintPreferences(bool constrain) {
    DisplayPreferences preferences;
    preferences.cap = FrameCapMode::unlimited;
    preferences.constrain_borderless = constrain;
    preferences.log = CollectBackendLine;
    return preferences;
}

void BorderlessConstraintTest() {
    ConstraintWindow window;
    const auto foreground = GetForegroundWindow();
    backendLines.clear();
    auto display = std::make_unique<DisplayBackend>(
        window.value, ConstraintPreferences(true));
    constrained = &display;
    const auto bounds = display->state().monitor.bounds;
    trace = {};
    // No wait_for_frame below: the constraint alone must hold the bounds.
    Require(SetWindowPos(window.value, nullptr, bounds.left + 10,
                         bounds.top + 10, 320, 240,
                         SWP_NOZORDER | SWP_NOACTIVATE),
            "foreign 320x240 resize of the borderless witness");
    Require(SameRectangle(Rectangle(window.value), bounds),
            "constraint keeps the monitor bounds without a frame");
    Require(!SawSize(trace.changed, 320, 240) &&
                !SawSize(trace.sizes, 320, 240),
            "no WM_WINDOWPOSCHANGED or WM_SIZE at the foreign size");
    SetWindowLongPtrW(window.value, GWL_STYLE, WS_OVERLAPPEDWINDOW);
    Require((GetWindowLongPtrW(window.value, GWL_STYLE) &
             (WS_CAPTION | WS_THICKFRAME)) == 0,
            "constraint removes a foreign caption and frame");
    const auto width = bounds.right - bounds.left;
    const auto height = bounds.bottom - bounds.top;
    const std::string position =
        "display-borderless-constrained kind=position req=320x240 out=" +
        std::to_string(width) + "x" + std::to_string(height) +
        " n=1 bridge=none";
    Require(LinesStartingWith("display-borderless-") == 2 &&
                backendLines[0] == position &&
                backendLines[1] == "display-borderless-constrained "
                                   "kind=style caption=1 frame=1 n=2 "
                                   "bridge=none",
            "two exact constraint lines");
    Require(display->corrections().constrained == 2 &&
                display->corrections().repaired == 0,
            "two constraints counted");
    Require(!IsWindowVisible(window.value) &&
                GetForegroundWindow() == foreground,
            "constraint neither shows nor focuses the witness");
    constrained = nullptr;
    display.reset();

    // Negative witness: the same gestures with C2 disabled.
    backendLines.clear();
    display = std::make_unique<DisplayBackend>(window.value,
                                               ConstraintPreferences(false));
    constrained = &display;
    Require(SetWindowPos(window.value, nullptr, bounds.left + 10,
                         bounds.top + 10, 320, 240,
                         SWP_NOZORDER | SWP_NOACTIVATE),
            "foreign resize without the constraint");
    SetWindowLongPtrW(window.value, GWL_STYLE, WS_OVERLAPPEDWINDOW);
    Require(Sized(Rectangle(window.value), 320, 240) &&
                display->corrections().constrained == 0,
            "witness: without C2 the foreign size is applied");
    display->wait_for_frame();
    Require(SameRectangle(Rectangle(window.value), bounds) &&
                LinesStartingWith("display-borderless-repaired style=1 "
                                  "rect=1 w=320 h=240 n=1") == 1 &&
                display->corrections().repaired == 1,
            "witness: only the next frame repairs, with one line");
    constrained = nullptr;
    display.reset();
    Require(GetForegroundWindow() == foreground,
            "constraint witness keeps the foreground");
}

void ConstraintExemptionTest() {
    ConstraintWindow window;
    const RECT original = Rectangle(window.value);
    const auto originalStyle = GetWindowLongPtrW(window.value, GWL_STYLE);
    const auto foreground = GetForegroundWindow();
    const auto monitors = DisplayBackend::monitors();
    backendLines.clear();
    auto display = std::make_unique<DisplayBackend>(
        window.value, ConstraintPreferences(true));
    constrained = &display;
    display->set_mode(WindowMode::windowed);
    Require((GetWindowLongPtrW(window.value, GWL_STYLE) &
             WS_OVERLAPPEDWINDOW) == WS_OVERLAPPEDWINDOW &&
                !SameRectangle(Rectangle(window.value),
                               display->state().monitor.bounds),
            "exemption: windowed mode keeps its frame and size");
    display->set_mode(WindowMode::borderless);
    for (const auto& monitor : monitors) {
        display->move_to_monitor(monitor.handle);
        Require(SameRectangle(Rectangle(window.value), monitor.bounds),
                "exemption: monitor migration by the bridge");
    }
    // The owner is cleared first, as ctx->backend.reset() does.
    display.reset();
    constrained = nullptr;
    Require(GetWindowLongPtrW(window.value, GWL_STYLE) == originalStyle &&
                SameRectangle(Rectangle(window.value), original),
            "exemption: destruction restores style and placement");
    Require(LinesStartingWith("display-borderless-") == 0,
            "exemption: no correction line for bridge operations");
    Require(GetForegroundWindow() == foreground,
            "exemption: foreground unchanged");
}

void MinimizedRestoreTest() {
    ConstraintWindow window;
    const auto foreground = GetForegroundWindow();
    backendLines.clear();
    auto display = std::make_unique<DisplayBackend>(
        window.value, ConstraintPreferences(true));
    constrained = &display;
    const auto bounds = display->state().monitor.bounds;
    ShowWindow(window.value, SW_SHOWMINNOACTIVE);
    Require(IsIconic(window.value) != FALSE, "minimized without activation");
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    Require(GetWindowPlacement(window.value, &placement) != FALSE,
            "read minimized placement");
    placement.rcNormalPosition = {bounds.left + 10, bounds.top + 10,
                                  bounds.left + 330, bounds.top + 250};
    placement.showCmd = SW_SHOWMINNOACTIVE;
    Require(SetWindowPlacement(window.value, &placement) != FALSE,
            "foreign 320x240 normal placement while minimized");
    const auto minimized = display->corrections();
    Require(IsIconic(window.value) != FALSE && minimized.constrained == 0 &&
                minimized.repaired == 0,
            "no correction while minimized");
    trace = {};
    ShowWindow(window.value, SW_SHOWNOACTIVATE);
    Require(IsIconic(window.value) == FALSE, "restored without activation");
    bool minimizedBit = false;
    for (const bool bit : trace.minimized_changing)
        minimizedBit |= bit;
    const RECT restored = Rectangle(window.value);
    const char* path = "none";
    if (SameRectangle(restored, bounds)) {
        path = "constraint";
        Require(display->corrections().constrained == 1 &&
                    display->corrections().repaired == 0 &&
                    LinesStartingWith(
                        "display-borderless-constrained kind=position "
                        "req=320x240 ") == 1,
                "restore path (a): one constraint, bounds at once");
    } else {
        path = "repair";
        Require(Sized(restored, 320, 240) && minimizedBit,
                "restore path (b): WS_MINIMIZE seen, foreign size first");
        display->wait_for_frame();
        Require(SameRectangle(Rectangle(window.value), bounds) &&
                    display->corrections().repaired == 1 &&
                    display->corrections().constrained == 0 &&
                    LinesStartingWith("display-borderless-repaired ") == 1,
                "restore path (b): one repair after one frame");
    }
    std::cout << "restore_path=" << path
              << " minimized_bit_seen=" << (minimizedBit ? 1 : 0) << "\n";
    Require(GetForegroundWindow() == foreground,
            "restore test keeps the foreground");
    constrained = nullptr;
    display.reset();
    Require(GetForegroundWindow() == foreground,
            "restore test teardown keeps the foreground");
}

void CorrectionStormTest() {
    ConstraintWindow window;
    const auto foreground = GetForegroundWindow();
    backendLines.clear();
    auto display = std::make_unique<DisplayBackend>(
        window.value, ConstraintPreferences(false));
    constrained = &display;
    const auto bounds = display->state().monitor.bounds;
    const auto start = GetTickCount64();
    std::vector<bool> repairedAt;
    for (unsigned cycle = 0; cycle != 6; ++cycle) {
        Require(SetWindowPos(window.value, nullptr, bounds.left + 10,
                             bounds.top + 10, 320, 240,
                             SWP_NOZORDER | SWP_NOACTIVATE),
                "storm: foreign resize");
        display->wait_for_frame();
        repairedAt.push_back(SameRectangle(Rectangle(window.value), bounds));
    }
    Require(GetTickCount64() - start < 1000, "storm: six cycles within 1 s");
    Require(repairedAt[0] && repairedAt[1] && repairedAt[2] && repairedAt[3] &&
                !repairedAt[4] && !repairedAt[5] &&
                Sized(Rectangle(window.value), 320, 240),
            "storm: four repairs, then the size of the host is kept");
    Require(display->corrections().repaired == 4 &&
                display->corrections().suspended == 2 &&
                display->corrections().storms == 1 &&
                LinesStartingWith("display-borderless-correction-storm ") ==
                    1 &&
                LinesStartingWith("display-borderless-repaired ") == 4,
            "storm: counters, one storm line, four repair lines");
    Sleep(1100);
    Require(SetWindowPos(window.value, nullptr, bounds.left + 10,
                         bounds.top + 10, 320, 240,
                         SWP_NOZORDER | SWP_NOACTIVATE),
            "storm: resize after the suspension");
    display->wait_for_frame();
    Require(SameRectangle(Rectangle(window.value), bounds) &&
                display->corrections().repaired == 5,
            "storm: repairs resume after 1.1 s");
    constrained = nullptr;
    display.reset();

    backendLines.clear();
    display = std::make_unique<DisplayBackend>(window.value,
                                               ConstraintPreferences(true));
    constrained = &display;
    bool held = true;
    for (unsigned cycle = 0; cycle != 6; ++cycle) {
        Require(SetWindowPos(window.value, nullptr, bounds.left + 10,
                             bounds.top + 10, 320, 240,
                             SWP_NOZORDER | SWP_NOACTIVATE),
                "storm: foreign resize with the constraint");
        held &= SameRectangle(Rectangle(window.value), bounds);
    }
    Require(held && display->corrections().constrained == 6 &&
                display->corrections().storms == 1 &&
                LinesStartingWith("display-borderless-correction-storm ") ==
                    1 &&
                LinesStartingWith("display-borderless-constrained ") <= 16,
            "storm: six constraints applied, one storm line, bounded lines");
    constrained = nullptr;
    display.reset();
    Require(GetForegroundWindow() == foreground,
            "storm test keeps the foreground");
}

// D1 of the review: the host shrinks its window in reaction to the
// bridge's WM_SIZE, or rewrites the bridge's own borderless request. Before
// the fix both escaped C2 during set_mode and every repair.
void HostReactionConstraintTest() {
    ConstraintWindow window;
    const auto foreground = GetForegroundWindow();
    backendLines.clear();
    host = {};
    auto display = std::make_unique<DisplayBackend>(
        window.value, ConstraintPreferences(true));
    constrained = &display;
    const auto bounds = display->state().monitor.bounds;
    const std::string out = std::to_string(bounds.right - bounds.left) + "x" +
                            std::to_string(bounds.bottom - bounds.top);
    const std::string position =
        "display-borderless-constrained kind=position req=320x240 out=" + out;

    // 1. A windowed target leaves the host's own size alone (exemption).
    host.react_to_size = true;
    display->set_mode(WindowMode::windowed);
    Require(Sized(Rectangle(window.value), 320, 240) &&
                display->corrections().constrained == 0,
            "reaction: a windowed target leaves the host's size alone");
    // 2. Back to borderless while the host reacts (C2 reproduction, step 2).
    const auto before = host.reactions;
    display->set_mode(WindowMode::borderless);
    Require(host.reactions > before && host.reactions_during_bridge == 0,
            "reaction: the host reacted inside set_mode(borderless)");
    Require(SameRectangle(Rectangle(window.value), bounds),
            "reaction: set_mode(borderless) keeps the monitor bounds");
    Require(
        display->corrections().constrained == 1 &&
            LinesStartingWith((position + " n=1 bridge=reaction").c_str()) == 1,
        "reaction: one constraint line naming the host reaction");
    // 3. A shrink C2 cannot see, then the repair (C2 reproduction, step 3).
    Require(SetWindowPos(window.value, nullptr, 0, 0, 320, 240,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE |
                             SWP_NOSENDCHANGING) != FALSE,
            "reaction: a shrink unseen by WM_WINDOWPOSCHANGING");
    Require(Sized(Rectangle(window.value), 320, 240),
            "reaction: the unseen shrink is applied");
    display->wait_for_frame();
    Require(SameRectangle(Rectangle(window.value), bounds) &&
                display->corrections().repaired == 1 &&
                display->corrections().constrained == 2,
            "reaction: the repair holds, its host reaction is constrained");
    for (unsigned frame = 0; frame != 8; ++frame) {
        display->wait_for_frame();
        Require(SameRectangle(Rectangle(window.value), bounds),
                "reaction: the bounds hold on the next frames");
    }
    Require(display->corrections().repaired == 1 &&
                display->corrections().storms == 0,
            "reaction: no repair loop, no storm");
    // 4. The host rewrites the size the bridge asks for.
    host.react_to_size = false;
    host.rewrite_size = true;
    display->set_mode(WindowMode::windowed);
    Require(Sized(Rectangle(window.value), 320, 240) &&
                display->corrections().constrained == 2,
            "rewrite: a windowed target stays exempt");
    display->set_mode(WindowMode::borderless);
    Require(SameRectangle(Rectangle(window.value), bounds) &&
                display->corrections().constrained == 3 &&
                LinesStartingWith((position + " n=3 bridge=altered").c_str()) ==
                    1,
            "rewrite: the bridge's own borderless request is restored");
    // 5. The same rewrite of a repair, outside any storm.
    Sleep(1100);
    host.rewrite_size = false;
    Require(SetWindowPos(window.value, nullptr, 0, 0, 320, 240,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE |
                             SWP_NOSENDCHANGING) != FALSE,
            "rewrite: a shrink unseen by WM_WINDOWPOSCHANGING");
    host.rewrite_size = true;
    display->wait_for_frame();
    host.rewrite_size = false;
    Require(SameRectangle(Rectangle(window.value), bounds) &&
                display->corrections().repaired == 2 &&
                display->corrections().constrained == 4 &&
                display->corrections().storms == 0,
            "rewrite: the repair's own request is restored");
    Require(LinesStartingWith("display-borderless-constrained ") == 4 &&
                LinesStartingWith("display-borderless-repaired ") == 2,
            "reaction: every correction has its line");
    for (const auto& line : backendLines)
        Require(!line.starts_with("display-borderless-constrained ") ||
                    !line.ends_with(" bridge=none"),
                "reaction: no constraint here is a plain foreign change");
    host = {};
    constrained = nullptr;
    display.reset();
    Require(!IsWindowVisible(window.value) &&
                GetForegroundWindow() == foreground,
            "reaction test neither shows nor focuses the witness");
}

// Review (tests, D1): the shared line budget of one backend, past 16
// corrections, and the final summary that keeps every counter.
void BoundedCorrectionLinesTest() {
    ConstraintWindow window;
    const auto foreground = GetForegroundWindow();
    const auto monitors = DisplayBackend::monitors();
    backendLines.clear();
    host = {};
    auto display = std::make_unique<DisplayBackend>(
        window.value, ConstraintPreferences(true));
    constrained = &display;
    const auto state = display->state();
    const auto bounds = state.monitor.bounds;
    for (unsigned gesture = 0; gesture != 20; ++gesture)
        Require(SetWindowPos(window.value, nullptr, bounds.left + 10,
                             bounds.top + 10, 320, 240,
                             SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
                "bounded: foreign resize");
    Require(SameRectangle(Rectangle(window.value), bounds) &&
                display->corrections().constrained == 20 &&
                LinesStartingWith("display-borderless-constrained ") == 16,
            "bounded: 20 constraints counted, 16 individual lines");
    Sleep(1100);
    Require(SetWindowPos(window.value, nullptr, 0, 0, 320, 240,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE |
                             SWP_NOSENDCHANGING) != FALSE,
            "bounded: a shrink unseen by WM_WINDOWPOSCHANGING");
    display->wait_for_frame();
    Require(SameRectangle(Rectangle(window.value), bounds) &&
                display->corrections().repaired == 1 &&
                LinesStartingWith("display-borderless-repaired ") == 0,
            "bounded: a repair past the budget is counted without a line");
    std::uint64_t migrations = 0;
    for (const auto& monitor : monitors) {
        if (monitor.handle == state.monitor.handle)
            continue;
        Require(SetWindowPos(window.value, nullptr, monitor.bounds.left,
                             monitor.bounds.top,
                             monitor.bounds.right - monitor.bounds.left,
                             monitor.bounds.bottom - monitor.bounds.top,
                             SWP_NOZORDER | SWP_NOACTIVATE) != FALSE,
                "bounded: shell-like migration to another monitor");
        migrations = 1;
        Require(display->corrections().migrations == 1 &&
                    LinesStartingWith("display-borderless-monitor-") == 0,
                "bounded: a migration past the budget is counted");
        break;
    }
    const auto n = display->corrections();
    Require(n.storms >= 1 &&
                LinesStartingWith("display-borderless-correction-storm ") ==
                    std::min<std::uint64_t>(n.storms, 16) &&
                LinesStartingWith("display-borderless-correction-summary ") ==
                    0,
            "bounded: storm lines match the storms, no summary yet");
    constrained = nullptr;
    display.reset();
    const std::string summary =
        "display-borderless-correction-summary constrained=20 repaired=1 "
        "suspended=0 storms=" +
        std::to_string(n.storms) + " migrations=" + std::to_string(migrations) +
        " skipped=0";
    Require(!backendLines.empty(), "bounded: lines were written");
    std::cout << "bounded_final=" << backendLines.back() << "\n";
    Require(backendLines.back() == summary,
            "bounded: the final summary carries every counter");
    Require(GetForegroundWindow() == foreground,
            "bounded test keeps the foreground");
}

void PaceAndUnfocusedHostTest() {
    Window window;
    DisplayPreferences preferences;
    preferences.mode = WindowMode::windowed;
    preferences.cap = FrameCapMode::fixed;
    preferences.fixed_fps = 60;
    DisplayBackend display(window.value, preferences);
    auto made = tdur::game::FixedStepScheduler::create({90, 60});
    Require(made.has_value(), "create independent fixed-step scheduler");
    auto scheduler = *made;
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto previous = start;
    uint64_t networkPhase = 0;
    uint64_t uiPhase = 0;
    for (unsigned frame = 0; frame < 61; ++frame) {
        display.wait_for_frame();
        const auto now = Clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(now -
                                                                 previous);
        Require(scheduler.advance(elapsed, false).has_value(),
                "advance unfocused host time");
        previous = now;
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        ++uiPhase;
        // A source-owned network service phase; this counter does not claim
        // to exercise retail network traffic or its internal simulation.
        ++networkPhase;
        while (scheduler.next_tick()) {
        }
        Require(!display.state().window_has_foreground,
                "window remains genuinely unfocused");
    }
    const double elapsed =
        std::chrono::duration<double>(Clock::now() - start).count();
    const auto snapshot = scheduler.snapshot();
    Require(elapsed >= 0.90 && elapsed <= 1.50,
            "fixed-deadline 60 Hz wall-time tolerance");
    Require(snapshot.physics_ticks >= 80 && snapshot.logic_ticks >= 53,
            "host simulation advances without focus");
    Require(snapshot.caught_up(), "no discarded simulation debt");
    Require(networkPhase == 61 && uiPhase == 61,
            "UI and network phases not skipped");
    std::cout << "unfocused_host seconds=" << elapsed
              << " physics=" << snapshot.physics_ticks
              << " logic=" << snapshot.logic_ticks << " ui=" << uiPhase
              << " network_phase=" << networkPhase << "\n";
}

constexpr std::array<D3DCOLOR, 4> Colors{
    D3DCOLOR_ARGB(255, 17, 34, 51), D3DCOLOR_ARGB(255, 68, 85, 102),
    D3DCOLOR_ARGB(255, 119, 136, 153), D3DCOLOR_ARGB(255, 170, 187, 204)};

void Pattern(IDirect3DDevice9* device, UINT width, UINT height,
             unsigned phase) {
    Checked(device->BeginScene(), "BeginScene reference");
    for (unsigned index = 0; index < 4; ++index) {
        const LONG left = (index & 1) ? static_cast<LONG>(width / 2) : 0;
        const LONG right = (index & 1) ? static_cast<LONG>(width)
                                       : static_cast<LONG>(width / 2);
        const LONG top = (index & 2) ? static_cast<LONG>(height / 2) : 0;
        const LONG bottom = (index & 2) ? static_cast<LONG>(height)
                                        : static_cast<LONG>(height / 2);
        const D3DRECT rectangle{left, top, right, bottom};
        Checked(device->Clear(1, &rectangle, D3DCLEAR_TARGET,
                              Colors[(index + phase) % Colors.size()], 1, 0),
                "D3D9 clear quadrant");
    }
    Checked(device->EndScene(), "EndScene before interop");
}

void CheckPattern(const DisplayPixels& pixels, UINT width, UINT height,
                  unsigned phase) {
    Require(pixels.width == width && pixels.height == height,
            "readback dimensions");
    Require(pixels.rgba.size() == static_cast<size_t>(width) * height * 4,
            "readback byte count");
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const unsigned quadrant =
                (x >= width / 2 ? 1U : 0U) + (y >= height / 2 ? 2U : 0U);
            const D3DCOLOR color = Colors[(quadrant + phase) % Colors.size()];
            const auto* rgba =
                pixels.rgba.data() + (static_cast<size_t>(y) * width + x) * 4;
            const std::array<unsigned, 4> expected{
                (color >> 16) & 255U, (color >> 8) & 255U, color & 255U, 255};
            for (size_t channel = 0; channel < expected.size(); ++channel) {
                if (std::to_integer<unsigned>(rgba[channel]) !=
                    expected[channel]) {
                    std::cerr
                        << "pixel mismatch x=" << x << " y=" << y
                        << " channel=" << channel
                        << " got=" << std::to_integer<unsigned>(rgba[channel])
                        << " expected=" << expected[channel] << "\n";
                    Require(false, "D3D9 to D3D12 pixel mismatch");
                }
            }
        }
    }
    Require(true, "all independent quadrant pixels match");
}

void InteropTest(bool maximumQuality) {
    Window window;
    DisplayPreferences preferences;
    preferences.mode = WindowMode::windowed;
    preferences.cap = FrameCapMode::unlimited;
    preferences.maximum_reference_quality = maximumQuality;
    DisplayBackend display(window.value, preferences);
    constexpr UINT width = 70;
    constexpr UINT height = 38;
    display.create_reference_device(width, height);
    const auto state = display.state();
    Require(state.d3d9on12_verified && display.device12() != nullptr,
            "real IDirect3DDevice9On12 and ID3D12Device obtained");
    if (!maximumQuality)
        Require(state.reference_msaa_samples == 1,
                "explicit non-MSAA reference");
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIFactory5> factory;
    Checked(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)),
            "fixture DXGI factory");
    const auto luid = display.device12()->GetAdapterLuid();
    Checked(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)),
            "real adapter by LUID");
    DXGI_ADAPTER_DESC1 description{};
    Checked(adapter->GetDesc1(&description), "real adapter description");
    std::wcout << L"D3D9On12 adapter=" << description.Description
               << L" feature_level=>=11_0 msaa=" << state.reference_msaa_samples
               << L" max_anisotropy=" << state.maximum_anisotropy
               << L" tearing_supported=" << state.tearing_supported << L"\n";
    for (unsigned phase = 0; phase < 4; ++phase) {
        Pattern(display.reference_device(), width, height, phase);
        const auto pixels =
            display.readback_surface9(display.reference_target());
        CheckPattern(pixels, width, height, phase);
        const HRESULT result =
            display.present_surface9(display.reference_target());
        Require(SUCCEEDED(result),
                "actual D3D12 flip presentation or occlusion status");
        std::cout << "present_result=0x" << std::hex
                  << static_cast<unsigned long>(result) << std::dec << "\n";
        // This reuse after ReturnUnderlyingResource catches missing fences or
        // stale descriptor/resource lifetimes on the next independently drawn
        // frame.
    }
    const auto presented = display.state();
    Require(!presented.exclusive_active,
            "windowed presentation remains nonexclusive");
    Require(presented.last_present.sync_interval == 0, "explicit sync0 used");
    Require(presented.last_present.vrr_eligible == presented.tearing_supported,
            "reported VRR eligibility reflects actual device feature query");
    Require(presented.last_present.present_flags ==
                (presented.tearing_supported ? DXGI_PRESENT_ALLOW_TEARING : 0U),
            "actual Present flags match feature query");
    RECT beforeFailure{};
    Require(GetWindowRect(window.value, &beforeFailure),
            "rectangle before refused exclusive");
    const LONG_PTR styleBeforeFailure =
        GetWindowLongPtrW(window.value, GWL_STYLE);
    bool exclusiveRejected = false;
    try {
        display.set_mode(WindowMode::exclusive);
    } catch (const DisplayError& error) {
        exclusiveRejected =
            error.result() == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    }
    Require(exclusiveRejected, "no exclusive ownership silently requested for "
                               "an unfocused hidden window");
    const auto afterFailure = display.state();
    Require(!afterFailure.exclusive_active &&
                afterFailure.requested_mode == WindowMode::windowed,
            "refused exclusive preserves actual/requested windowed state");
    RECT afterRectangle{};
    Require(GetWindowRect(window.value, &afterRectangle),
            "rectangle after refused exclusive");
    Require(SameRectangle(beforeFailure, afterRectangle) &&
                GetWindowLongPtrW(window.value, GWL_STYLE) ==
                    styleBeforeFailure,
            "refused exclusive preserves rectangle and style");
    bool nullSurfaceRejected = false;
    try {
        (void)display.present_surface9(nullptr);
    } catch (const std::invalid_argument&) {
        nullSurfaceRejected = true;
    }
    Require(nullSurfaceRejected,
            "null surface rejected before recording GPU transitions");
    ComPtr<IDirect3DSurface9> unsupported;
    Checked(display.reference_device()->CreateRenderTarget(
                width, height, D3DFMT_R5G6B5, D3DMULTISAMPLE_NONE, 0, FALSE,
                &unsupported, nullptr),
            "fixture unsupported-format target");
    bool formatRejected = false;
    try {
        (void)display.present_surface9(unsupported.Get());
    } catch (const DisplayError& error) {
        formatRejected = error.result() == DXGI_ERROR_UNSUPPORTED;
    }
    Require(formatRejected,
            "unsupported source format rejected before target barriers");
    display.set_mode(WindowMode::borderless);
    Pattern(display.reference_device(), width, height, 1);
    Require(SUCCEEDED(display.present_surface9(display.reference_target())),
            "present after borderless ResizeBuffers");
    display.set_mode(WindowMode::windowed);
    Pattern(display.reference_device(), width, height, 2);
    Require(SUCCEEDED(display.present_surface9(display.reference_target())),
            "present after windowed ResizeBuffers");
}

void ClassicManagedTest() {
    Window window;
    DisplayPreferences preferences;
    preferences.mode = WindowMode::windowed;
    preferences.cap = FrameCapMode::unlimited;
    DisplayBackend display(window.value, preferences);
    auto* factory = display.factory9on12_compat();
    D3DPRESENT_PARAMETERS parameters{};
    parameters.BackBufferWidth = 64;
    parameters.BackBufferHeight = 32;
    parameters.BackBufferFormat = D3DFMT_A8R8G8B8;
    parameters.BackBufferCount = 1;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = display.render_window();
    parameters.Windowed = TRUE;
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    ComPtr<IDirect3DDevice9> device;
    Checked(factory->CreateDevice(
                D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window.value,
                D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
                &parameters, &device),
            "classic Direct3DCreate9On12 / CreateDevice");
    display.attach_device9(device.Get());
    Require(display.state().d3d9on12_verified,
            "classic device really backed by D3D12");
    ComPtr<IDirect3DSurface9> target;
    Checked(device->GetRenderTarget(0, &target), "classic backbuffer surface");
    ComPtr<IDirect3DTexture9> managed;
    Checked(device->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                  &managed, nullptr),
            "D3DPOOL_MANAGED texture supported");
    D3DLOCKED_RECT locked{};
    Checked(managed->LockRect(0, &locked, nullptr, 0), "managed texture lock");
    constexpr D3DCOLOR color = D3DCOLOR_ARGB(255, 13, 101, 229);
    for (unsigned row = 0; row < 4; ++row) {
        auto* pixels = reinterpret_cast<D3DCOLOR*>(
            static_cast<std::byte*>(locked.pBits) +
            static_cast<size_t>(row) * locked.Pitch);
        for (unsigned column = 0; column < 4; ++column)
            pixels[column] = color;
    }
    Checked(managed->UnlockRect(0), "managed texture unlock");
    struct Vertex {
        float x, y, z, rhw, u, v;
    };
    const std::array<Vertex, 4> quad{{{-0.5f, -0.5f, 0.5f, 1, 0, 0},
                                      {63.5f, -0.5f, 0.5f, 1, 1, 0},
                                      {-0.5f, 31.5f, 0.5f, 1, 0, 1},
                                      {63.5f, 31.5f, 0.5f, 1, 1, 1}}};
    Checked(device->SetRenderState(D3DRS_ZENABLE, FALSE), "classic depth off");
    Checked(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE),
            "classic cull off");
    Checked(device->SetRenderState(D3DRS_LIGHTING, FALSE),
            "classic lighting off");
    Checked(device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1),
            "classic select texture color");
    Checked(device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE),
            "classic texture argument");
    Checked(device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1),
            "classic texture alpha");
    Checked(device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE),
            "classic alpha argument");
    Checked(device->SetTexture(0, managed.Get()),
            "classic bind managed texture");
    Checked(device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1),
            "classic transformed vertex declaration");
    Checked(device->BeginScene(), "classic begin scene");
    Checked(device->Clear(0, nullptr, D3DCLEAR_TARGET,
                          D3DCOLOR_ARGB(255, 255, 0, 255), 1, 0),
            "classic distinguishable clear");
    Checked(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad.data(),
                                    sizeof(Vertex)),
            "classic draw using real managed texture");
    Checked(device->EndScene(), "classic end before checkout");
    const auto result = display.readback_surface9(target.Get());
    Require(result.width == 64 && result.height == 32,
            "classic GPU readback dimensions");
    for (size_t pixel = 0; pixel < 64 * 32; ++pixel) {
        const auto* rgba = result.rgba.data() + pixel * 4;
        Require(std::to_integer<unsigned>(rgba[0]) == 13 &&
                    std::to_integer<unsigned>(rgba[1]) == 101 &&
                    std::to_integer<unsigned>(rgba[2]) == 229 &&
                    std::to_integer<unsigned>(rgba[3]) == 255,
                "classic managed texture draw verified through real D3D12 "
                "readback");
    }
    Require(SUCCEEDED(display.present_surface9(target.Get())),
            "classic backbuffer presented through D3D12");
    std::cout << "classic_managed_draw verified_pixels=2048\n";
}

void VisibleExclusiveTest() {
    Window window;
    SetWindowTextW(window.value,
                   L"TDU reconstruction - D3D9On12 witness (no game)");
    DisplayPreferences preferences;
    preferences.mode = WindowMode::borderless;
    preferences.maximum_reference_quality = false;
    DisplayBackend display(window.value, preferences);
    display.create_reference_device(70, 38);
    Pattern(display.reference_device(), 70, 38, 0);
    // Only this explicitly selected fixture may request foreground. The
    // backend itself never fakes activation or captures global input.
    ShowWindow(window.value, SW_SHOW);
    SetForegroundWindow(window.value);
    using Clock = std::chrono::steady_clock;
    const auto visibleStart = Clock::now();
    const auto beforeExclusive = display.state();
    RECT beforeRectangle{};
    GetWindowRect(window.value, &beforeRectangle);
    std::cout << "exclusive_attempt foreground_window="
              << beforeExclusive.window_has_foreground << " foreground_process="
              << beforeExclusive.process_has_foreground
              << " visible=" << IsWindowVisible(window.value) << "\n";
    try {
        display.set_mode(WindowMode::exclusive);
    } catch (const DisplayError& error) {
        RECT afterRectangle{};
        GetWindowRect(window.value, &afterRectangle);
        const auto restored = display.state();
        Require(!restored.exclusive_active &&
                    restored.requested_mode == WindowMode::borderless,
                "exclusive refusal leaves honest borderless state");
        Require(SameRectangle(beforeRectangle, afterRectangle),
                "exclusive refusal restores exact window rectangle");
        Checked(display.present_surface9(display.reference_target()),
                "borderless still renders after exclusive refusal");
        ShowWindow(window.value, SW_HIDE);
        std::cout << "exclusive_refused_hresult=0x" << std::hex
                  << static_cast<unsigned long>(error.result()) << std::dec
                  << " rollback_verified=1\n";
        throw;
    }
    const auto exclusive = display.state();
    Require(exclusive.exclusive_active,
            "real DXGI exclusive ownership verified");
    Require(exclusive.requested_mode == WindowMode::exclusive,
            "exclusive request tracked");
    uint64_t presents = 0;
    while (Clock::now() - visibleStart < std::chrono::milliseconds(1300)) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        display.wait_for_frame();
        Checked(display.present_surface9(display.reference_target()),
                "actual exclusive D3D12 Present");
        const auto policy = display.state().last_present;
        Require(policy.present_flags == 0,
                "no windowed tearing flag in exclusive");
        Require(
            !policy.vrr_eligible,
            "exclusive VRR eligibility not inferred from windowed DXGI query");
        ++presents;
    }
    display.set_mode(WindowMode::borderless);
    Require(!display.state().exclusive_active, "exclusive ownership released");
    Checked(display.present_surface9(display.reference_target()),
            "return to borderless D3D12 Present");
    display.set_mode(WindowMode::windowed);
    Require(!display.state().exclusive_active, "return to true windowed state");
    Checked(display.present_surface9(display.reference_target()),
            "return to windowed D3D12 Present");
    ShowWindow(window.value, SW_HIDE);
    const double visibleSeconds =
        std::chrono::duration<double>(Clock::now() - visibleStart).count();
    std::cout << "visible_exclusive presents=" << presents
              << " visible_seconds=" << visibleSeconds
              << " desktop_refresh=" << exclusive.monitor.refresh_numerator
              << "/" << exclusive.monitor.refresh_denominator << "\n";
    Require(visibleSeconds < 2,
            "visible exclusive fixture bounded to two seconds");
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool windowsOnly =
            argc > 1 && std::string_view(argv[1]) == "--window-only";
        const bool interopOnly =
            argc > 1 && std::string_view(argv[1]) == "--interop-only";
        const bool visibleExclusive =
            argc > 1 && std::string_view(argv[1]) == "--exclusive-visible";
        // The window suite shows witness windows on the monitors and needs an
        // unchanged foreground for its whole run: it fails, and disturbs the
        // user, when someone works at the station. Opt-in only, for runs
        // without a user (CTest reports the skip, SKIP_RETURN_CODE 77).
        if (!visibleExclusive && !interopOnly) {
            std::array<wchar_t, 4> optIn{};
            const auto length = GetEnvironmentVariableW(
                L"TDUR_TESTS_PREMIER_PLAN", optIn.data(),
                static_cast<DWORD>(optIn.size()));
            if (length != 1 || optIn[0] != L'1') {
                std::cout << "display_api_witness skipped: the window suite "
                             "needs the foreground; set "
                             "TDUR_TESTS_PREMIER_PLAN=1 when nobody uses the "
                             "station\n";
                return 77;
            }
        }
        SetProcessDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        if (visibleExclusive) {
            VisibleExclusiveTest();
        } else if (!interopOnly) {
            PolicyTest();
            SwitchAndGuardPolicyTest();
            WindowTest();
            BorderlessRecoveryTest();
            WindowOperationScopeTest();
            BorderlessConstraintTest();
            ConstraintExemptionTest();
            MinimizedRestoreTest();
            CorrectionStormTest();
            HostReactionConstraintTest();
            BoundedCorrectionLinesTest();
            PaceAndUnfocusedHostTest();
        }
        if (!visibleExclusive && !windowsOnly) {
            InteropTest(false);
            InteropTest(true);
            ClassicManagedTest();
        }
        std::cout << "display_api_witness checks=" << checks << " failures=0\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "display_api_witness checks=" << checks
                  << " failed: " << error.what() << "\n";
        return 1;
    }
}
