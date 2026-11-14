// C3 witness for the opt-in window diagnostics. Messages and calls are built
// in memory: no window is created, moved, shown or activated, and no desktop
// API is called. HWND values are opaque test numbers.
#include "tdur/mod/window_diagnostics.hpp"

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

void Collect(const CursorRecord& record) noexcept {
    records.push_back(
        {record.event, record.reason, record.error, record.caller});
    SetLastError(999);
}

HWND Window(std::uintptr_t value) { return reinterpret_cast<HWND>(value); }

bool Has(const std::string& text, const char* part) {
    return text.find(part) != std::string::npos;
}

const Seen& Last() { return records.back(); }

LPARAM Pointer(const void* value) { return reinterpret_cast<LPARAM>(value); }

void format_cases() {
    records.clear();
    WindowDiagnostics diagnostics(Collect);
    const auto game = Window(0x1000);
    const void* caller = &format_cases;
    diagnostics.bind(game);
    expect(records.size() == 1 && Last().event == "window-diag-bound" &&
               Last().reason.empty(),
           "bind: one line without reason");
    diagnostics.bind(game);
    expect(records.size() == 1, "bind: same HWND writes nothing");

    SetLastError(31);
    WindowCall setpos{};
    setpos.op = WindowOp::set_pos;
    setpos.width = 1128;
    setpos.height = 634;
    setpos.flags = SWP_NOZORDER | SWP_NOACTIVATE;
    setpos.order = "none";
    diagnostics.api_call(game, setpos, CallerOrigin::main, caller);
    expect(GetLastError() == 31, "api_call keeps the last error");
    expect(Last().event == "window-op" &&
               Last().reason ==
                   "op=setpos w=1128 h=634 flags=0x0014 size=1 move=1 "
                   "order=none fits=0 origin=main" &&
               Last().caller == caller,
           "setpos: exact line with caller");

    WindowCall fits = setpos;
    fits.flags = SWP_NOMOVE | SWP_NOZORDER;
    fits.fits_monitor = true;
    fits.order = "topmost";
    diagnostics.api_call(game, fits, CallerOrigin::proxy, caller);
    expect(Last().reason == "op=setpos w=1128 h=634 flags=0x0006 size=1 "
                            "move=0 order=topmost fits=1 origin=proxy",
           "setpos: no move, fits and order");

    WindowCall move{};
    move.op = WindowOp::move;
    move.width = 2560;
    move.height = 1440;
    move.fits_monitor = true;
    diagnostics.api_call(game, move, CallerOrigin::user32, caller);
    expect(Last().reason == "op=move w=2560 h=1440 fits=1 origin=user32",
           "move: exact line");

    WindowCall style{};
    style.op = WindowOp::set_style;
    style.style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    diagnostics.api_call(game, style, CallerOrigin::main, caller);
    expect(Last().reason == "op=style caption=1 frame=1 popup=0 minimize=0 "
                            "maximize=0 visible=1 origin=main",
           "style: classes, not values");
    style.style = WS_POPUP | WS_MINIMIZE | WS_MAXIMIZE;
    diagnostics.api_call(game, style, CallerOrigin::proxy, caller);
    expect(Last().reason == "op=style caption=0 frame=0 popup=1 minimize=1 "
                            "maximize=1 visible=0 origin=proxy",
           "style: popup, minimize and maximize");

    WindowCall exstyle{};
    exstyle.op = WindowOp::set_exstyle;
    exstyle.style = WS_EX_CLIENTEDGE | WS_EX_TOPMOST;
    diagnostics.api_call(game, exstyle, CallerOrigin::other, caller);
    expect(Last().reason == "op=exstyle edges=1 topmost=1 origin=other",
           "exstyle: edges and topmost");

    WindowCall placement{};
    placement.op = WindowOp::placement;
    placement.command = SW_SHOWMINNOACTIVE;
    placement.width = 320;
    placement.height = 240;
    diagnostics.api_call(game, placement, CallerOrigin::main, caller);
    expect(Last().reason == "op=placement show=7 w=320 h=240 origin=main",
           "placement: command and normal size");

    WindowCall show{};
    show.op = WindowOp::show;
    show.command = SW_MINIMIZE;
    diagnostics.api_call(game, show, CallerOrigin::main, caller);
    expect(Last().reason == "op=show cmd=6 origin=main", "show: command");
    show.op = WindowOp::show_async;
    show.command = SW_RESTORE;
    diagnostics.api_call(game, show, CallerOrigin::unresolved, caller);
    expect(Last().reason == "op=show-async cmd=9 origin=unresolved",
           "show-async: command");

    diagnostics.api_failed(game, WindowOp::set_pos, ERROR_ACCESS_DENIED,
                           CallerOrigin::main, caller);
    expect(Last().event == "window-op-failed" &&
               Last().reason == "op=setpos origin=main" &&
               Last().error == ERROR_ACCESS_DENIED,
           "failed: operation, origin and error");

    WINDOWPOS position{};
    position.cx = 2560;
    position.cy = 1440;
    position.flags = SWP_NOZORDER | SWP_NOACTIVATE;
    MessageContext context{};
    context.constrained = true;
    context.requested_width = 1128;
    context.requested_height = 634;
    diagnostics.message(game, WM_WINDOWPOSCHANGING, 0, Pointer(&position),
                        context);
    expect(Last().event == "window-msg" &&
               Last().reason ==
                   "msg=poschanging req=1128x634 out=2560x1440 flags=0x0014 "
                   "constrained=1 internal=0 during=none/none",
           "poschanging: constrained request and applied size");
    position.cx = 1128;
    position.cy = 634;
    context = {};
    context.internal = true;
    diagnostics.message(game, WM_WINDOWPOSCHANGING, 0, Pointer(&position),
                        context);
    expect(Last().reason ==
               "msg=poschanging req=1128x634 out=1128x634 flags=0x0014 "
               "constrained=0 internal=1 during=none/none",
           "poschanging: unconstrained, internal");
    context = {};
    context.minimized = true;
    position.flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE;
    diagnostics.message(game, WM_WINDOWPOSCHANGED, 0, Pointer(&position),
                        context);
    expect(Last().reason == "msg=poschanged w=1128 h=634 flags=0x0016 "
                            "minimized=1 internal=0 during=none/none",
           "poschanged: size, flags and minimized");
    const auto before = records.size();
    position.flags = SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER;
    diagnostics.message(game, WM_WINDOWPOSCHANGING, 0, Pointer(&position),
                        MessageContext{});
    diagnostics.message(game, WM_WINDOWPOSCHANGED, 0, Pointer(&position),
                        MessageContext{});
    expect(records.size() == before,
           "position messages without size, move or visibility are silent");
    position.flags = SWP_NOSIZE | SWP_NOMOVE | SWP_SHOWWINDOW;
    diagnostics.message(game, WM_WINDOWPOSCHANGED, 0, Pointer(&position),
                        MessageContext{});
    expect(records.size() == before + 1, "visibility change is written");

    diagnostics.message(game, WM_SIZE, SIZE_MINIMIZED, MAKELPARAM(0, 0),
                        MessageContext{});
    expect(Last().reason ==
               "msg=size type=1 w=0 h=0 internal=0 during=none/none",
           "size: type and client size");
    STYLESTRUCT change{};
    change.styleNew = WS_POPUP | WS_VISIBLE;
    diagnostics.message(game, WM_STYLECHANGED, static_cast<WPARAM>(GWL_STYLE),
                        Pointer(&change), MessageContext{});
    expect(Last().reason == "msg=stylechanged caption=0 frame=0 popup=1 "
                            "minimize=0 maximize=0 internal=0",
           "stylechanged: classes");
    change.styleNew = WS_EX_WINDOWEDGE;
    diagnostics.message(game, WM_STYLECHANGED,
                        static_cast<WPARAM>(static_cast<LONG_PTR>(GWL_EXSTYLE)),
                        Pointer(&change), MessageContext{});
    expect(Last().reason == "msg=exstylechanged edges=1 topmost=0 internal=0",
           "exstylechanged: edges");
    diagnostics.message(game, WM_SYSCOMMAND, SC_MINIMIZE | 2, 0,
                        MessageContext{});
    expect(Last().reason ==
               "msg=syscommand cmd=minimize internal=0 during=none/none",
           "syscommand: low bits ignored");
    diagnostics.message(game, WM_SYSCOMMAND, SC_KEYMENU, 0, MessageContext{});
    expect(Has(Last().reason, "cmd=keymenu"), "syscommand: keymenu");
    diagnostics.message(game, WM_SHOWWINDOW, FALSE, SW_PARENTCLOSING,
                        MessageContext{});
    expect(Last().reason == "msg=showwindow show=0 status=1",
           "showwindow: show and status");
    diagnostics.message(game, WM_DISPLAYCHANGE, 32, MAKELPARAM(2560, 1440),
                        MessageContext{});
    expect(Last().reason == "msg=displaychange w=2560 h=1440 bpp=32",
           "displaychange: mode only");
    const auto quiet = records.size();
    diagnostics.message(game, WM_MOUSEMOVE, 0, MAKELPARAM(11, 22),
                        MessageContext{});
    diagnostics.message(game, WM_ACTIVATE, WA_ACTIVE, 0, MessageContext{});
    expect(records.size() == quiet, "other messages ignored");

    D3DPRESENT_PARAMETERS parameters{};
    parameters.Windowed = FALSE;
    parameters.BackBufferWidth = 1128;
    parameters.BackBufferHeight = 634;
    parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    parameters.BackBufferCount = 1;
    parameters.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    parameters.FullScreen_RefreshRateInHz = 160;
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    parameters.hDeviceWindow = Window(0xABCDEF);
    diagnostics.present_request("reset", DescribePresentRequest(parameters),
                                caller);
    expect(Last().event == "display-present-request" &&
               Last().reason ==
                   "call=reset windowed=0 bb=1128x634 fmt=22 count=1 msaa=4 "
                   "swap=1 flags=0x1 refresh=160 interval=0x1" &&
               Last().caller == caller,
           "present request: requested values, with caller");

    diagnostics.unbind(Window(0x2000));
    expect(diagnostics.bound_to(game), "unbind: another HWND is ignored");
    diagnostics.unbind(game);
    expect(Last().event == "window-diag-unbound" && !diagnostics.bound_to(game),
           "unbind: one line");

    for (const auto& seen : records)
        // HWND values 0x1000 and 0xABCDEF, in hexadecimal or decimal.
        expect(!Has(seen.reason, "abcdef") && !Has(seen.reason, "ABCDEF") &&
                   !Has(seen.reason, "1000") && !Has(seen.reason, "4096") &&
                   !Has(seen.reason, "11259375"),
               "privacy: no handle value or coordinate");
}

void filter_cases() {
    records.clear();
    WindowDiagnostics diagnostics(Collect);
    const auto game = Window(0x1000);
    const auto other = Window(0x3000);
    WindowCall call{};
    call.op = WindowOp::show;
    diagnostics.api_call(game, call, CallerOrigin::main, nullptr);
    expect(records.empty(), "filter: nothing before bind");
    diagnostics.bind(game);
    records.clear();
    diagnostics.api_call(other, call, CallerOrigin::main, nullptr);
    diagnostics.api_failed(other, WindowOp::show, 5, CallerOrigin::main,
                           nullptr);
    WINDOWPOS position{};
    diagnostics.message(other, WM_WINDOWPOSCHANGED, 0, Pointer(&position),
                        MessageContext{});
    diagnostics.message(other, WM_SIZE, 0, 0, MessageContext{});
    expect(records.empty(), "filter: another HWND writes nothing");
    diagnostics.activation(WM_ACTIVATEAPP, TRUE);
    expect(records.empty(), "filter: another HWND is not even counted");
}

void budget_cases() {
    records.clear();
    WindowDiagnostics diagnostics(Collect);
    const auto game = Window(0x1000);
    diagnostics.bind(game);
    records.clear();
    WindowCall setpos{};
    setpos.op = WindowOp::set_pos;
    setpos.width = 320;
    setpos.height = 240;
    for (unsigned index = 0; index != 30; ++index)
        diagnostics.api_call(game, setpos, CallerOrigin::main, nullptr);
    WindowCall show{};
    show.op = WindowOp::show;
    for (unsigned index = 0; index != 5; ++index)
        diagnostics.api_call(game, show, CallerOrigin::proxy, nullptr);
    WindowCall style{};
    style.op = WindowOp::set_style;
    diagnostics.api_call(game, style, CallerOrigin::d3d9, nullptr);
    WINDOWPOS position{};
    position.cx = 320;
    position.cy = 240;
    MessageContext constrained{};
    constrained.constrained = true;
    diagnostics.message(game, WM_WINDOWPOSCHANGING, 0, Pointer(&position),
                        constrained);
    diagnostics.message(game, WM_WINDOWPOSCHANGED, 0, Pointer(&position),
                        MessageContext{});
    diagnostics.message(game, WM_SIZE, 0, 0, MessageContext{});
    diagnostics.message(game, WM_SYSCOMMAND, SC_RESTORE, 0, MessageContext{});
    expect(records.size() == 32 && !diagnostics.line_available(),
           "budget: 32 individual lines per period");
    diagnostics.activation(WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, 0));
    expect(records.size() == 33 && Last().event == "window-summary" &&
               Last().reason ==
                   "ops=36 proxy=5 main=30 ext=1 setpos=30 show=5 style=1 "
                   "placement=0 move=0 changing=1 changed=1 size=1 syscmd=1 "
                   "constrained=1",
           "budget: exact summary at the next marker");
    expect(diagnostics.line_available(), "budget: marker reopens the window");
    diagnostics.activation(WM_ACTIVATEAPP, FALSE);
    expect(records.size() == 33, "budget: no summary for an empty period");
    diagnostics.activation(WM_MOUSEMOVE, 0);
    diagnostics.api_call(game, show, CallerOrigin::main, nullptr);
    expect(records.size() == 34, "budget: new period writes again");

    WindowPeriod widest{};
    const std::uint32_t most = 0xFFFFFFFFu;
    widest = {most, most, most, most, most, most, most,
              most, most, most, most, most, most, most};
    CursorRecord record{"window-summary"};
    FormatWindowSummary(widest, record);
    const std::string expected =
        "ops=4294967295 proxy=4294967295 main=4294967295 ext=4294967295 "
        "setpos=4294967295 show=4294967295 style=4294967295 "
        "placement=4294967295 move=4294967295 changing=4294967295 "
        "changed=4294967295 size=4294967295 syscmd=4294967295 "
        "constrained=4294967295";
    expect(std::string(record.reason) == expected &&
               expected.size() < sizeof(record.reason),
           "budget: widest summary written whole");

    records.clear();
    WindowDiagnostics capped(Collect, WindowLimits{32, 64, 10});
    capped.bind(game);
    for (unsigned round = 0; round != 100; ++round) {
        capped.api_call(game, show, CallerOrigin::main, nullptr);
        capped.activation(WM_ACTIVATEAPP, round & 1U);
    }
    expect(records.size() == 10 && capped.lines_written() == 10,
           "cap: total lines bounded");

    records.clear();
    WindowDiagnostics presents(Collect);
    D3DPRESENT_PARAMETERS parameters{};
    for (unsigned index = 0; index != 100; ++index)
        presents.present_request("create", DescribePresentRequest(parameters),
                                 nullptr);
    expect(records.size() == 64, "cap: 64 presentation requests");
}

void scope_cases() {
    records.clear();
    WindowDiagnostics diagnostics(Collect);
    const auto game = Window(0x1000);
    diagnostics.bind(game);
    WINDOWPOS position{};
    position.cx = 640;
    position.cy = 480;
    expect(!CurrentWindowCall().active, "scope: none at rest");
    {
        WindowCallScope outer(WindowOp::show, CallerOrigin::main);
        diagnostics.message(game, WM_WINDOWPOSCHANGED, 0, Pointer(&position),
                            MessageContext{});
        expect(Has(Last().reason, "during=show/main"),
               "scope: message attributed to the call in progress");
        {
            WindowCallScope inner(WindowOp::set_pos, CallerOrigin::proxy);
            diagnostics.message(game, WM_SIZE, 0, MAKELPARAM(640, 480),
                                MessageContext{});
            expect(Has(Last().reason, "during=setpos/proxy"),
                   "scope: nested call attributed to the innermost");
        }
        diagnostics.message(game, WM_SYSCOMMAND, SC_MAXIMIZE, 0,
                            MessageContext{});
        expect(Has(Last().reason, "cmd=maximize") &&
                   Has(Last().reason, "during=show/main"),
               "scope: outer call restored after the nested one");
    }
    diagnostics.message(game, WM_SIZE, 0, 0, MessageContext{});
    expect(Has(Last().reason, "during=none/none") &&
               !CurrentWindowCall().active,
           "scope: none after the outer call");
    {
        WindowCallScope retail(WindowOp::show_async, CallerOrigin::retail_site);
        diagnostics.message(game, WM_SIZE, 0, 0, MessageContext{});
        expect(Has(Last().reason, "during=show-async/retail-site"),
               "scope: widest attribution fits");
    }
}

void optional_cases() {
    records.clear();
    SetActiveWindowDiagnostics(nullptr);
    const auto game = Window(0x1000);
    D3DPRESENT_PARAMETERS parameters{};
    TdurWindowDiagnosticsBind(game);
    TdurWindowActivationMarker(WM_ACTIVATEAPP, TRUE);
    WINDOWPOS position{};
    TdurWindowDiagnosticsMessage(game, WM_WINDOWPOSCHANGED, 0,
                                 Pointer(&position), MessageContext{});
    TdurWindowDiagnosticsPresentRequest("reset", parameters, nullptr);
    TdurWindowDiagnosticsUnbind(game);
    expect(records.empty() && !TdurWindowDiagnosticsBound(game),
           "optional: no instance, no line");

    WindowDiagnostics diagnostics(Collect);
    SetActiveWindowDiagnostics(&diagnostics);
    TdurWindowDiagnosticsBind(game);
    expect(TdurWindowDiagnosticsBound(game) && records.size() == 1,
           "optional: active instance bound");
    position.cx = 800;
    position.cy = 600;
    TdurWindowDiagnosticsMessage(game, WM_WINDOWPOSCHANGED, 0,
                                 Pointer(&position), MessageContext{});
    TdurWindowDiagnosticsPresentRequest("create", parameters, &optional_cases);
    expect(records.size() == 3 && Has(records[1].reason, "w=800 h=600") &&
               Has(records[2].reason, "call=create windowed=0"),
           "optional: messages and requests reach the instance");
    TdurWindowDiagnosticsUnbind(game);
    SetActiveWindowDiagnostics(nullptr);
    TdurWindowDiagnosticsBind(game);
    expect(records.size() == 4, "optional: cleared instance is silent");
}

} // namespace

int main() {
    format_cases();
    filter_cases();
    budget_cases();
    scope_cases();
    optional_cases();
    std::printf("{\"suite\":\"window_diagnostics\",\"checks\":%d,"
                "\"failures\":%d,\"game_executed\":false}\n",
                checks, failures);
    return failures == 0 ? 0 : 1;
}
