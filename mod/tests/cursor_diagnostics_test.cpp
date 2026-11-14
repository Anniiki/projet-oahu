// Witness for the opt-in cursor diagnostics. API calls go through an
// injected table of source-owned fakes, so no desktop cursor, clip or capture
// state changes. The subclass case owns a hidden popup window that is never
// shown or activated; activation messages are sent to it, not produced.
#include "tdur/mod/cursor_diagnostics.hpp"

#include <commctrl.h>

#include <climits>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace tdur::mod;

int failures = 0;
int checks = 0;

void expect(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s\n", label);
    }
}

struct Seen {
    std::string event;
    std::string reason;
    DWORD error{};
    const void* caller{};
};
std::vector<Seen> records;

// A sink that clobbers the thread last error, as a real logger might.
void Collect(const CursorRecord& record) noexcept {
    records.push_back(
        {record.event, record.reason, record.error, record.caller});
    SetLastError(999);
}

struct Fake {
    int show_calls{}, set_calls{}, clip_calls{}, release_calls{};
    BOOL show_arg{-1};
    HCURSOR set_arg{};
    const RECT* clip_arg{};
    int show_result{};
    HCURSOR set_result{};
    BOOL clip_result{TRUE}, release_result{TRUE};
    DWORD error{};
} fake;

int WINAPI FakeShowCursor(BOOL show) {
    ++fake.show_calls;
    fake.show_arg = show;
    SetLastError(fake.error);
    return fake.show_result;
}
HCURSOR WINAPI FakeSetCursor(HCURSOR cursor) {
    ++fake.set_calls;
    fake.set_arg = cursor;
    SetLastError(fake.error);
    return fake.set_result;
}
BOOL WINAPI FakeClipCursor(const RECT* rect) {
    ++fake.clip_calls;
    fake.clip_arg = rect;
    SetLastError(fake.error);
    return fake.clip_result;
}
BOOL WINAPI FakeReleaseCapture() {
    ++fake.release_calls;
    SetLastError(fake.error);
    return fake.release_result;
}

CursorApiTable FakeTable() {
    return {FakeShowCursor, FakeSetCursor, FakeClipCursor, FakeReleaseCapture};
}

HCURSOR Handle(std::uintptr_t value) {
    return reinterpret_cast<HCURSOR>(value);
}

bool Has(const std::string& text, const char* part) {
    return text.find(part) != std::string::npos;
}

void propagation_cases() {
    records.clear();
    fake = {};
    CursorDiagnostics diagnostics(FakeTable(), Collect);
    const void* caller = &propagation_cases;

    fake.show_result = -1;
    fake.error = 1234;
    const int count = diagnostics.show_cursor(FALSE, caller);
    expect(count == -1 && GetLastError() == 1234, "show: result and error");
    expect(fake.show_calls == 1 && fake.show_arg == FALSE,
           "show: one call, same argument");
    expect(records.size() == 1 &&
               records[0].reason == "show-cursor arg=false count=-1" &&
               records[0].caller == caller && records[0].error == 0,
           "show: classified line with caller");

    fake.set_result = Handle(0x12345678);
    fake.error = 0x77;
    const HCURSOR previous = diagnostics.set_cursor(Handle(0x0BADF00D), caller);
    expect(previous == Handle(0x12345678) && GetLastError() == 0x77,
           "set: previous handle and error unchanged");
    expect(fake.set_calls == 1 && fake.set_arg == Handle(0x0BADF00D),
           "set: one call, same handle");
    expect(records.size() == 2 &&
               records[1].reason == "set-cursor arg=handle previous=handle",
           "set: handles classified, not printed");

    const RECT rect{11, 22, 3344, 5566};
    fake.clip_result = FALSE;
    fake.error = ERROR_ACCESS_DENIED;
    expect(diagnostics.clip_cursor(&rect, caller) == FALSE &&
               GetLastError() == ERROR_ACCESS_DENIED,
           "clip: failure and error unchanged");
    expect(fake.clip_calls == 1 && fake.clip_arg == &rect,
           "clip: one call, same pointer");
    expect(records.size() == 3 &&
               records[2].reason == "clip-cursor arg=rect result=0" &&
               records[2].error == ERROR_ACCESS_DENIED,
           "clip: rect classified, error recorded");

    fake.clip_result = TRUE;
    fake.error = 0;
    expect(diagnostics.clip_cursor(nullptr, caller) == TRUE &&
               fake.clip_arg == nullptr && records.size() == 4 &&
               records[3].reason == "clip-cursor arg=null result=1" &&
               records[3].error == 0,
           "clip: release of the clip");

    fake.release_result = FALSE;
    fake.error = 1400;
    expect(diagnostics.release_capture(caller) == FALSE &&
               GetLastError() == 1400 && fake.release_calls == 1,
           "release: failure and error unchanged");
    expect(records.size() == 5 &&
               records[4].reason == "release-capture result=0" &&
               records[4].error == 1400,
           "release: classified line");

    for (const auto& seen : records)
        expect(!Has(seen.reason, "12345678") && !Has(seen.reason, "BADF00D") &&
                   !Has(seen.reason, "badf00d") && !Has(seen.reason, "3344") &&
                   !Has(seen.reason, "5566") && !Has(seen.reason, "22"),
               "privacy: no handle value or coordinate");
}

void bound_cases() {
    records.clear();
    fake = {};
    CursorDiagnostics diagnostics(FakeTable(), Collect);
    fake.set_result = Handle(0x10);
    for (int index = 0; index < 100000; ++index)
        diagnostics.set_cursor(Handle(0x20), nullptr);
    expect(records.size() == 16, "flood: start-up window of 16 lines");
    diagnostics.activation(WM_ACTIVATEAPP, TRUE);
    // The summary starts with a bare word so every count parses as a field;
    // the whole line is compared, so no trailing field can be cut.
    expect(records.size() == 18 && records[16].event == "cursor-summary" &&
               records[16].reason ==
                   "period show1=0 show0=0 hidden=0 set0=0 set1=100000 clip=0 "
                   "unclip=0 clipfail=0 release=0 releasefail=0" &&
               records[17].event == "cursor-activation" &&
               records[17].reason == "activateapp active=1",
           "flood: exact summary then marker");
    for (int index = 0; index < 100000; ++index)
        diagnostics.set_cursor(nullptr, nullptr);
    expect(records.size() == 34, "flood: one new window of 16 lines");
    expect(fake.set_calls == 200000, "flood: every call forwarded");

    records.clear();
    CursorLimits tight{16, 256, 50};
    CursorDiagnostics capped(FakeTable(), Collect, tight);
    for (int round = 0; round < 1000; ++round) {
        capped.show_cursor(TRUE, nullptr);
        capped.activation(WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, 0));
    }
    expect(records.size() == 50 && capped.lines_written() == 50 &&
               capped.markers_seen() == 1000,
           "cap: total lines bounded, markers still counted");

    records.clear();
    CursorDiagnostics few(FakeTable(), Collect, CursorLimits{0, 3, 4096});
    for (int round = 0; round < 10; ++round)
        few.activation(WM_ACTIVATEAPP, FALSE);
    std::size_t markers = 0;
    for (const auto& seen : records)
        if (seen.event == "cursor-activation")
            ++markers;
    expect(markers == 3 && records.size() == 3 && few.markers_seen() == 10,
           "cap: activation lines bounded, no empty summary");
}

// Regression of the review: a 96-byte reason cut the summary after
// "unclip=10000", losing clipfail, release and releasefail.
void summary_cases() {
    records.clear();
    fake = {};
    fake.show_result = -3;
    fake.clip_result = FALSE;
    fake.release_result = FALSE;
    CursorDiagnostics diagnostics(FakeTable(), Collect);
    RECT owned{};
    for (unsigned index = 0; index != 100001; ++index) {
        diagnostics.show_cursor(TRUE, nullptr);
        diagnostics.show_cursor(FALSE, nullptr);
        diagnostics.set_cursor(nullptr, nullptr);
        diagnostics.set_cursor(Handle(1), nullptr);
        diagnostics.clip_cursor(nullptr, nullptr);
        diagnostics.clip_cursor(&owned, nullptr);
        diagnostics.release_capture(nullptr);
    }
    diagnostics.activation(WM_ACTIVATEAPP, FALSE);
    expect(records.size() == 18 && records[16].event == "cursor-summary" &&
               records[16].reason ==
                   "period show1=100001 show0=100001 hidden=200002 "
                   "set0=100001 set1=100001 clip=100001 unclip=100001 "
                   "clipfail=200002 release=0 releasefail=100001",
           "summary: all ten fields exact after a mixed flood");

    // Distinct values catch a field printed under another name.
    CursorRecord distinct{"cursor-summary"};
    FormatCursorSummary(CursorPeriod{1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, distinct);
    expect(std::string(distinct.reason) ==
               "period show1=1 show0=2 hidden=3 set0=4 set1=5 clip=6 "
               "unclip=7 clipfail=8 release=9 releasefail=10",
           "summary: each counter under its own name");

    const std::uint32_t most = 0xFFFFFFFFu;
    CursorRecord widest{"cursor-summary"};
    FormatCursorSummary(CursorPeriod{most, most, most, most, most, most, most,
                                     most, most, most},
                        widest);
    const std::string expected =
        "period show1=4294967295 show0=4294967295 hidden=4294967295 "
        "set0=4294967295 set1=4294967295 clip=4294967295 "
        "unclip=4294967295 clipfail=4294967295 release=4294967295 "
        "releasefail=4294967295";
    expect(std::string(widest.reason) == expected && expected.size() == 186 &&
               expected.size() < sizeof(widest.reason),
           "summary: complete with every counter at UINT32_MAX");

    records.clear();
    fake = {};
    fake.show_result = INT_MIN;
    CursorDiagnostics longest(FakeTable(), Collect);
    longest.show_cursor(FALSE, nullptr);
    expect(records.size() == 1 &&
               records[0].reason == "show-cursor arg=false count=-2147483648",
           "show: widest count written whole");
}

void activation_cases() {
    records.clear();
    fake = {};
    CursorDiagnostics diagnostics(FakeTable(), Collect,
                                  CursorLimits{0, 256, 4096});
    for (const int result : {-1, -2, -3}) {
        fake.show_result = result;
        diagnostics.show_cursor(FALSE, nullptr);
    }
    fake.show_result = -2;
    diagnostics.show_cursor(TRUE, nullptr);
    expect(records.empty(), "window: zero budget writes no operation line");
    diagnostics.activation(WM_ACTIVATE, MAKEWPARAM(WA_CLICKACTIVE, 1));
    expect(records.size() == 2 &&
               Has(records[0].reason, "show1=1 show0=3 hidden=4") &&
               records[1].reason == "activate state=click minimized=1",
           "activation: summary counts and WM_ACTIVATE classes");
    diagnostics.activation(WM_MOUSEMOVE, 0);
    expect(records.size() == 2 && diagnostics.markers_seen() == 1,
           "activation: other messages ignored");
    diagnostics.activation(WM_ACTIVATEAPP, FALSE);
    expect(records.size() == 3 && records[2].reason == "activateapp active=0",
           "activation: no summary for an empty period");
}

void optional_cases() {
    using Setting = CursorDiagnosticsSetting;
    expect(ParseCursorDiagnosticsSetting(nullptr, 0, ERROR_ENVVAR_NOT_FOUND) ==
               Setting::disabled,
           "setting: absent disables");
    expect(ParseCursorDiagnosticsSetting(L"1", 1, ERROR_SUCCESS) ==
               Setting::enabled,
           "setting: exact 1 enables");
    expect(ParseCursorDiagnosticsSetting(L"", 0, ERROR_SUCCESS) ==
                   Setting::invalid &&
               ParseCursorDiagnosticsSetting(L"0", 1, ERROR_SUCCESS) ==
                   Setting::invalid &&
               ParseCursorDiagnosticsSetting(L"11", 2, ERROR_SUCCESS) ==
                   Setting::invalid &&
               ParseCursorDiagnosticsSetting(L"true", 4, ERROR_SUCCESS) ==
                   Setting::invalid,
           "setting: any other value is invalid");

    records.clear();
    SetActiveCursorDiagnostics(nullptr);
    TdurCursorActivationMarker(WM_ACTIVATEAPP, TRUE);
    expect(records.empty(), "optional: no instance, marker has no effect");
    CursorDiagnostics diagnostics(FakeTable(), Collect);
    SetActiveCursorDiagnostics(&diagnostics);
    TdurCursorActivationMarker(WM_ACTIVATEAPP, TRUE);
    expect(records.size() == 1, "optional: active instance receives marker");
    SetActiveCursorDiagnostics(nullptr);
    TdurCursorActivationMarker(WM_ACTIVATEAPP, FALSE);
    expect(records.size() == 1, "optional: cleared instance is silent");
}

// C5: injected classifier. A stands for the retail site, B for the main
// image elsewhere, C for the system dinput8.dll.
// Writable, so identical-data folding can never merge their addresses.
char site_a{}, site_b{}, site_c{};
CallerOrigin FakeClassifier(const void* caller) noexcept {
    SetLastError(4242); // a real classifier may clobber the last error
    if (caller == &site_a)
        return CallerOrigin::retail_site;
    if (caller == &site_b)
        return CallerOrigin::main;
    if (caller == &site_c)
        return CallerOrigin::dinput8;
    return CallerOrigin::unresolved;
}

std::size_t LinesFrom(const void* caller) {
    std::size_t count = 0;
    for (const auto& seen : records)
        if (seen.event == "cursor-op" && seen.caller == caller)
            ++count;
    return count;
}

void RetailThenOthers(CursorDiagnostics& diagnostics) {
    fake.show_result = -1;
    for (unsigned index = 0; index != 20; ++index)
        diagnostics.show_cursor(FALSE, &site_a);
    fake.show_result = 0;
    fake.error = 77;
    diagnostics.show_cursor(TRUE, &site_c);
    diagnostics.show_cursor(TRUE, &site_b);
}

void origin_cases() {
    records.clear();
    fake = {};
    CursorDiagnostics diagnostics(FakeTable(), Collect, CursorLimits{},
                                  FakeClassifier);
    RetailThenOthers(diagnostics);
    expect(GetLastError() == 77, "origin: classifier keeps the last error");
    expect(LinesFrom(&site_a) == 2,
           "origin: retail site detailed only twice per window");
    bool dinput8 = false;
    for (const auto& seen : records)
        dinput8 |= seen.caller == &site_c &&
                   seen.reason == "show-cursor arg=true count=0 origin=dinput8";
    expect(dinput8, "origin: external show written with its origin");
    expect(LinesFrom(&site_b) == 1 &&
               records.back().reason ==
                   "show-cursor arg=true count=0 origin=main",
           "origin: main image classified");
    expect(records.front().reason ==
               "show-cursor arg=false count=-1 origin=retail-site",
           "origin: retail site classified");
    diagnostics.activation(WM_ACTIVATEAPP, TRUE);
    const auto& summary = records[records.size() - 2];
    expect(summary.event == "cursor-summary" &&
               Has(summary.reason, "show1=2 show0=20") &&
               Has(summary.reason, " ext1=1 ext0=0"),
           "origin: summary counts external shows");
    // The marker reopens the retail budget.
    const auto before = records.size();
    diagnostics.show_cursor(FALSE, &site_a);
    expect(records.size() == before + 1,
           "origin: activation reopens the retail budget");

    // Negative witness: same sequence without a classifier (earlier format).
    records.clear();
    fake = {};
    CursorDiagnostics plain(FakeTable(), Collect);
    RetailThenOthers(plain);
    expect(LinesFrom(&site_a) == 16 && LinesFrom(&site_c) == 0 &&
               LinesFrom(&site_b) == 0,
           "origin witness: retail flood hides the external show");
    plain.activation(WM_ACTIVATEAPP, TRUE);
    bool ext = false;
    for (const auto& seen : records)
        ext |= Has(seen.reason, "ext1") || Has(seen.reason, "origin=");
    expect(!ext, "origin witness: no origin field without a classifier");
}

void origin_names() {
    expect(CallerOriginFromModule(L"D3D9.DLL", false, false) ==
                   CallerOrigin::d3d9 &&
               CallerOriginFromModule(L"d3d9on12.dll", false, false) ==
                   CallerOrigin::d3d9on12 &&
               CallerOriginFromModule(L"dxgi.dll", false, false) ==
                   CallerOrigin::dxgi &&
               CallerOriginFromModule(L"D3D12Core.dll", false, false) ==
                   CallerOrigin::d3d12 &&
               CallerOriginFromModule(L"d3d12.dll", false, false) ==
                   CallerOrigin::d3d12 &&
               CallerOriginFromModule(L"win32u.dll", false, false) ==
                   CallerOrigin::user32 &&
               CallerOriginFromModule(L"USER32.dll", false, false) ==
                   CallerOrigin::user32,
           "origin: fixed system module table, case-insensitive");
    expect(CallerOriginFromModule(L"dinput8.dll", false, true) ==
                   CallerOrigin::proxy &&
               CallerOriginFromModule(L"dinput8.dll", false, false) ==
                   CallerOrigin::dinput8,
           "origin: proxy decided by handle, not by name");
    expect(CallerOriginFromModule(L"kernel32.dll", false, false) ==
                   CallerOrigin::other &&
               CallerOriginFromModule(L"anything.exe", true, false) ==
                   CallerOrigin::main &&
               CallerOriginFromModule(L"", false, false) ==
                   CallerOrigin::unresolved &&
               CallerOriginFromModule(nullptr, false, false) ==
                   CallerOrigin::unresolved,
           "origin: other, main and unresolved");
    const char* names[] = {"unresolved", "main",     "retail-site", "proxy",
                           "d3d9",       "d3d9on12", "dxgi",        "d3d12",
                           "dinput8",    "user32",   "other"};
    bool all = true;
    for (unsigned index = 0; index != 11; ++index)
        all &=
            std::string(CallerOriginName(static_cast<CallerOrigin>(index))) ==
            names[index];
    expect(all, "origin: every class name");

    const std::uint32_t most = 0xFFFFFFFFu;
    CursorRecord widest{"cursor-summary"};
    FormatCursorSummary(CursorPeriod{most, most, most, most, most, most, most,
                                     most, most, most, most, most},
                        widest, true);
    const std::string expected =
        "period show1=4294967295 show0=4294967295 hidden=4294967295 "
        "set0=4294967295 set1=4294967295 clip=4294967295 "
        "unclip=4294967295 clipfail=4294967295 release=4294967295 "
        "releasefail=4294967295 ext1=4294967295 ext0=4294967295";
    expect(std::string(widest.reason) == expected && expected.size() == 218 &&
               expected.size() < sizeof(widest.reason) &&
               sizeof(widest.reason) == 256,
           "origin: widest summary with origin fits in 256");
}

std::vector<std::string> order;

LRESULT CALLBACK BaseProcedure(HWND window, UINT message, WPARAM wparam,
                               LPARAM lparam) {
    if (message == WM_ACTIVATEAPP) {
        order.push_back(wparam ? "base:activateapp1" : "base:activateapp0");
        return 0;
    }
    if (message == WM_ACTIVATE) {
        order.push_back(LOWORD(wparam) == WA_INACTIVE ? "base:activate0"
                                                      : "base:activate");
        return 0; // never reach DefWindowProc, which could take focus
    }
    if (message == WM_NCDESTROY)
        order.push_back("base:ncdestroy");
    return DefWindowProcW(window, message, wparam, lparam);
}

// Mirrors the proposed display_hooks.cpp order: cleanup on WM_NCDESTROY,
// then the marker, then DefSubclassProc for every message.
LRESULT CALLBACK FixtureSubclass(HWND window, UINT message, WPARAM wparam,
                                 LPARAM lparam, UINT_PTR id, DWORD_PTR) {
    if (message == WM_NCDESTROY) {
        order.push_back("subclass:ncdestroy");
        RemoveWindowSubclass(window, FixtureSubclass, id);
        order.push_back("subclass:removed");
    }
    TdurCursorActivationMarker(message, wparam);
    return DefSubclassProc(window, message, wparam, lparam);
}

void subclass_cases() {
    records.clear();
    order.clear();
    CursorDiagnostics diagnostics(FakeTable(), Collect);
    SetActiveCursorDiagnostics(&diagnostics);
    const HINSTANCE module = GetModuleHandleW(nullptr);
    WNDCLASSEXW type{sizeof(WNDCLASSEXW)};
    type.lpfnWndProc = BaseProcedure;
    type.hInstance = module;
    type.lpszClassName = L"TdurC015CursorWitness";
    const ATOM atom = RegisterClassExW(&type);
    const HWND foreground = GetForegroundWindow();
    const HWND capture = GetCapture();
    const HWND window = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"TdurC015CursorWitness", L"",
        WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, module, nullptr);
    expect(atom != 0 && window != nullptr && !IsWindowVisible(window),
           "subclass: hidden owned window");
    expect(SetWindowSubclass(window, FixtureSubclass, 15, 0) != FALSE,
           "subclass: installed");
    SendMessageW(window, WM_ACTIVATEAPP, TRUE, 0);
    SendMessageW(window, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, 0), 0);
    expect(records.size() == 2 && records[0].reason == "activateapp active=1" &&
               records[1].reason == "activate state=inactive minimized=0",
           "subclass: markers recorded");
    expect(order.size() == 2 && order[0] == "base:activateapp1" &&
               order[1] == "base:activate0",
           "subclass: messages reach the base procedure unchanged");
    expect(GetForegroundWindow() != window &&
               GetForegroundWindow() == foreground && GetCapture() == capture,
           "subclass: no foreground or capture change");
    expect(DestroyWindow(window) != FALSE, "subclass: destroyed");
    expect(order.size() == 5 && order[2] == "subclass:ncdestroy" &&
               order[3] == "subclass:removed" && order[4] == "base:ncdestroy",
           "subclass: removed before the base sees WM_NCDESTROY");
    expect(records.size() == 2, "subclass: destruction writes no marker");
    SetActiveCursorDiagnostics(nullptr);
    UnregisterClassW(L"TdurC015CursorWitness", module);
}

} // namespace

int main() {
    propagation_cases();
    bound_cases();
    summary_cases();
    activation_cases();
    optional_cases();
    origin_cases();
    origin_names();
    subclass_cases();
    std::printf("{\"suite\":\"cursor_diagnostics\",\"checks\":%d,"
                "\"failures\":%d,\"game_executed\":false}\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
