// No desktop mouse, visibility counter, clipping, focus, capture, foreground
// or WinEvent hook is changed: every desktop API is injected.
#include "tdur/mod/foreground_cursor.hpp"

#include <dwmapi.h>

#include <climits>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <initializer_list>
#include <string>

namespace {
unsigned checks{}, failures{};
void expect(bool condition, const char* description) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", description);
    }
}
HWND window(unsigned value) {
    return reinterpret_cast<HWND>(static_cast<UINT_PTR>(value));
}
HCURSOR cursor(unsigned value) {
    return reinterpret_cast<HCURSOR>(static_cast<UINT_PTR>(value));
}
// Synthetic desktop. The game window 1, its child 5 and its other top-level
// window 6 belong to process 100; window 2, which takes the foreground, to
// process 200; the overlay root 0xBEEF, its part 0xBEF0 and the rootless
// window 7 to process 300. Window 8 has no process.
const HWND game_child = window(5);
const HWND game_popup = window(6);
const HWND rootless = window(7);
const HWND lifeless = window(8);
const HWND overlay = window(0xBEEF);
const HWND overlay_part = window(0xBEF0);
struct Fake {
    DWORD thread = 7;
    HWND foreground = window(1);
    HWND capture = window(1);
    HCURSOR cursor = ::cursor(2);
    RECT clip{5, 10, 15, 20};
    int count = -3;
    unsigned shows{}, hides{}, releases{}, unclips{}, sets{};
    bool child{}, get_clip_succeeds = true;
    // Pointer ownership.
    DWORD process = 100;
    ULONGLONG tick = 10;
    POINT point{31337, 27183};
    bool point_fails{}, point_matched{};
    HWND under = window(1);
    unsigned probes{};
    UINT root_flags{};
    BOOL overlay_visible = TRUE;
    DWORD overlay_cloaked{};
    bool attribute_fails{};
    bool cursor_info_fails{};
    int key_down{};
    const wchar_t* overlay_image = L"X:\\fixture\\apps\\GameBar.exe";
    const wchar_t* overlay_class = L"ApplicationFrameWindow";
    // Foreground WinEvent hook.
    unsigned hooks{}, unhooks{};
    DWORD hook_min{}, hook_max{}, hook_process = 99, hook_thread = 99;
    DWORD hook_flags{};
    HMODULE hook_module = reinterpret_cast<HMODULE>(static_cast<UINT_PTR>(1));
    WINEVENTPROC hook_proc{};
    HWINEVENTHOOK hook_result =
        reinterpret_cast<HWINEVENTHOOK>(static_cast<UINT_PTR>(0x4A4B));
    HWINEVENTHOOK unhooked{};
    // A call made from inside ReleaseCapture (WM_CAPTURECHANGED reaching the
    // game), with this window under the pointer by then.
    tdur::mod::ForegroundCursor* reenter{};
    HWND reenter_under{};
} fake;
DWORD WINAPI CurrentThread() { return fake.thread; }
HWND WINAPI Foreground() { return fake.foreground; }
BOOL WINAPI Child(HWND, HWND) { return fake.child; }
HWND WINAPI Capture() { return fake.capture; }
BOOL WINAPI Release() {
    ++fake.releases;
    fake.capture = nullptr;
    if (auto* policy = fake.reenter) {
        fake.reenter = nullptr;
        fake.under = fake.reenter_under;
        fake.tick += 50;
        policy->poll();
    }
    return TRUE;
}
BOOL WINAPI Clip(const RECT* rectangle) {
    if (rectangle)
        fake.clip = *rectangle;
    else
        ++fake.unclips;
    return TRUE;
}
BOOL WINAPI GetClip(RECT* rectangle) {
    if (!fake.get_clip_succeeds)
        return FALSE;
    *rectangle = fake.clip;
    return TRUE;
}
int WINAPI Show(BOOL show) {
    show ? ++fake.shows : ++fake.hides;
    fake.count += show ? 1 : -1;
    return fake.count;
}
HCURSOR WINAPI Set(HCURSOR value) {
    const auto previous = fake.cursor;
    fake.cursor = value;
    ++fake.sets;
    return previous;
}
DWORD WINAPI CurrentProcess() { return fake.process; }
ULONGLONG WINAPI Tick() { return fake.tick; }
BOOL WINAPI CursorPosition(POINT* point) {
    if (fake.point_fails)
        return FALSE;
    *point = fake.point;
    return TRUE;
}
HWND WINAPI FromPoint(POINT point) {
    ++fake.probes;
    fake.point_matched = point.x == fake.point.x && point.y == fake.point.y;
    return fake.under;
}
HWND WINAPI Ancestor(HWND hit, UINT flags) {
    fake.root_flags = flags;
    if (flags != GA_ROOT || hit == rootless)
        return nullptr;
    if (hit == game_child)
        return window(1);
    if (hit == overlay_part)
        return overlay;
    return hit;
}
DWORD WINAPI WindowProcess(HWND target, DWORD* process) {
    DWORD owner = 0;
    if (target == window(1) || target == game_child || target == game_popup)
        owner = 100;
    else if (target == window(2))
        owner = 200;
    else if (target == overlay || target == overlay_part || target == rootless)
        owner = 300;
    if (process)
        *process = owner;
    return owner ? 11 : 0;
}
BOOL WINAPI Visible(HWND target) {
    return target == overlay ? fake.overlay_visible : TRUE;
}
HRESULT WINAPI Attribute(HWND target, DWORD attribute, void* value,
                         DWORD size) {
    if (attribute != static_cast<DWORD>(DWMWA_CLOAKED) || size != sizeof(DWORD))
        return E_INVALIDARG;
    // The output is undefined on failure: a nonzero value stands for it.
    if (fake.attribute_fails) {
        *static_cast<DWORD*>(value) = 0xFFFFFFFFu;
        return E_FAIL;
    }
    *static_cast<DWORD*>(value) = target == overlay ? fake.overlay_cloaked : 0;
    return S_OK;
}
HWINEVENTHOOK WINAPI EventHook(DWORD minimum, DWORD maximum, HMODULE module,
                               WINEVENTPROC procedure, DWORD process,
                               DWORD thread, DWORD flags) {
    ++fake.hooks;
    fake.hook_min = minimum;
    fake.hook_max = maximum;
    fake.hook_module = module;
    fake.hook_proc = procedure;
    fake.hook_process = process;
    fake.hook_thread = thread;
    fake.hook_flags = flags;
    return fake.hook_result;
}
BOOL WINAPI EventUnhook(HWINEVENTHOOK hook) {
    ++fake.unhooks;
    fake.unhooked = hook;
    return TRUE;
}
// The game thread's own counter decides what shows over the game window.
BOOL WINAPI CursorInfo(CURSORINFO* info) {
    if (fake.cursor_info_fails || info->cbSize != sizeof(CURSORINFO))
        return FALSE;
    info->flags = fake.count >= 0 ? CURSOR_SHOWING : 0;
    return TRUE;
}
SHORT WINAPI KeyState(int key) {
    return key == fake.key_down ? static_cast<SHORT>(SHRT_MIN) : 0;
}
DWORD WINAPI ProcessImage(DWORD process, wchar_t* buffer, DWORD capacity) {
    const wchar_t* path = process == 300   ? fake.overlay_image
                          : process == 200 ? L"X:\\fixture\\shell\\explorer.exe"
                                           : L"X:\\fixture\\game\\Game.exe";
    if (!path)
        return 0;
    const auto length = std::wcslen(path);
    if (length >= capacity)
        return 0;
    std::wmemcpy(buffer, path, length + 1);
    return static_cast<DWORD>(length);
}
int WINAPI ClassName(HWND target, wchar_t* buffer, int capacity) {
    const wchar_t* name = target == overlay        ? fake.overlay_class
                          : target == overlay_part ? L"OverlayPart"
                          : target == window(2)    ? L"Shell_TrayWnd"
                          : target == rootless     ? L"RootlessWindow"
                                                   : L"GameWindow";
    if (!name || capacity <= 0)
        return 0;
    int length = 0;
    while (name[length] && length + 1 < capacity) {
        buffer[length] = name[length];
        ++length;
    }
    buffer[length] = L'\0';
    return length;
}
// The last 16 policy log lines, event and reason; transition counts and the
// leak check cover every line.
struct Line {
    std::string event, reason;
};
Line lines[16];
unsigned line_count{}, released_lines{}, returned_lines{}, capped_lines{};
bool leaked{};
void Captured(const char* event, const char* reason) noexcept {
    lines[line_count % 16] = {event, reason ? reason : ""};
    ++line_count;
    if (std::strcmp(event, "foreground-cursor-released") == 0)
        ++released_lines;
    if (std::strcmp(event, "foreground-cursor-returned") == 0)
        ++returned_lines;
    if (std::strcmp(event, "foreground-cursor-transitions-capped") == 0)
        ++capped_lines;
    // Pointer coordinates, window and hook handles of the fake desktop.
    for (const char* secret : {"31337", "27183", "48879", "48880", "beef",
                               "BEEF", "bef0", "BEF0", "19019", "4a4b", "4A4B"})
        if (reason && std::strstr(reason, secret))
            leaked = true;
}
const Line& LastLine() { return lines[(line_count + 15) % 16]; }
void Reset(int count) {
    fake = {};
    fake.count = count;
    line_count = released_lines = returned_lines = capped_lines = 0;
    leaked = false;
}
auto Api() {
    return tdur::mod::ForegroundCursorApi{.current_thread = CurrentThread,
                                          .foreground = Foreground,
                                          .is_child = Child,
                                          .capture = Capture,
                                          .release_capture = Release,
                                          .clip = Clip,
                                          .get_clip = GetClip,
                                          .show = Show,
                                          .set = Set,
                                          .current_process = CurrentProcess,
                                          .tick = Tick,
                                          .cursor_position = CursorPosition,
                                          .window_from_point = FromPoint,
                                          .ancestor = Ancestor,
                                          .window_process = WindowProcess,
                                          .visible = Visible,
                                          .window_attribute = Attribute,
                                          .event_hook = EventHook,
                                          .event_unhook = EventUnhook,
                                          .cursor_info = CursorInfo,
                                          .key_state = KeyState,
                                          .process_image = ProcessImage,
                                          .class_name = ClassName};
}
// The foreground WinEvent as Windows delivers it to the installed procedure.
void Event(HWINEVENTHOOK hook = nullptr,
           DWORD event = EVENT_SYSTEM_FOREGROUND) {
    if (fake.hook_proc)
        fake.hook_proc(hook ? hook : fake.hook_result, event, fake.foreground,
                       OBJID_WINDOW, CHILDID_SELF, 0, 0);
}
// The hooks of cursor_diagnostics.cpp go through intercept(): SetCursorPos,
// ClipCursor with a rectangle and SetCapture are refused this way.
bool Refused(tdur::mod::ForegroundCursor& policy) {
    return policy.intercept(true);
}
void Lose(tdur::mod::ForegroundCursor& policy) {
    policy.before_message(WM_ACTIVATEAPP, FALSE);
    fake.foreground = window(2);
    policy.after_message(WM_ACTIVATEAPP, FALSE);
}
void Gain(tdur::mod::ForegroundCursor& policy) {
    fake.foreground = window(1);
    policy.before_message(WM_ACTIVATEAPP, TRUE);
    policy.after_message(WM_ACTIVATEAPP, TRUE);
}
void ownership() {
    fake = {};
    tdur::mod::ForegroundCursor policy(Api());
    expect(!policy.background(), "no bound game HWND leaves APIs unchanged");
    policy.before_message(WM_ACTIVATE, WA_INACTIVE);
    policy.after_message(WM_ACTIVATE, WA_INACTIVE);
    expect(!fake.shows && !fake.releases && !fake.sets,
           "unbound activation has no side effect");
    policy.bind(window(1), 8, cursor(3));
    expect(!policy.bound_to(window(1)), "another thread cannot register HWND");
    policy.bind(window(1), 7, cursor(3));
    expect(policy.bound_to(window(1)) && !policy.background(),
           "foreground owner is bound and active");
    fake.foreground = window(2);
    expect(policy.background(), "real foreground loss blocks cursor APIs");
    fake.child = true;
    expect(!policy.background(), "a foreground child belongs to the game");
    fake.child = false;
    fake.thread = 9;
    expect(!policy.background(), "other threads retain their own cursor state");
    policy.after_message(WM_ACTIVATEAPP, FALSE);
    expect(!fake.shows, "non-owner cannot change visibility");
    fake.thread = 7;
    policy.unbind(window(2));
    expect(policy.bound_to(window(1)), "another HWND cannot clear ownership");
    policy.unbind(window(1));
    expect(!policy.bound_to(window(1)), "owner teardown clears registration");
}
void alt_tab_balance() {
    fake = {};
    tdur::mod::ForegroundCursor policy(Api());
    policy.bind(window(1), 7, cursor(3));
    policy.clipped(&fake.clip, TRUE);
    policy.before_message(WM_ACTIVATEAPP, FALSE);
    expect(policy.background(), "block before native deactivation work");
    fake.foreground = window(2);
    policy.after_message(WM_ACTIVATEAPP, FALSE);
    expect(fake.releases == 1 && !fake.capture,
           "release only capture belonging to our HWND");
    expect(fake.unclips == 1, "release the clipping rectangle we installed");
    expect(fake.count == 0 && fake.shows == 3,
           "make hidden cursor visible with a bounded adjustment");
    expect(fake.cursor == cursor(3) && fake.sets == 1,
           "provide the stock arrow without moving the pointer");
    expect(policy.visibility_adjustments() == 3,
           "record only our visibility counter adjustments");
    policy.before_message(WM_ACTIVATE, WA_INACTIVE);
    policy.after_message(WM_ACTIVATE, WA_INACTIVE);
    expect(fake.shows == 3 && fake.releases == 1 && fake.unclips == 1,
           "paired activation messages do not accumulate adjustments");
    bool loops_terminate = true;
    for (unsigned i = 0; i != 10000; ++i)
        loops_terminate &= policy.background_show_result(FALSE) < 0 &&
                           policy.background_show_result(TRUE) >= 0;
    expect(loops_terminate && fake.count == 0 && fake.shows == 3,
           "background hide/show loops terminate without desktop counter debt");
    Gain(policy);
    expect(fake.count == -1 && fake.hides == 1 &&
               !policy.visibility_adjustments() && !policy.background(),
           "a single hide given back on return (C1 policy)");
    Gain(policy);
    expect(fake.hides == 1, "duplicate activation does not restore twice");
    for (unsigned i = 0; i != 4; ++i) {
        Lose(policy);
        Gain(policy);
    }
    expect(fake.count == -1 && fake.shows == 7 && fake.hides == 5,
           "repeated Alt+Tab cycles do not deepen the counter");
}
void external_ownership() {
    fake = {};
    tdur::mod::ForegroundCursor policy(Api());
    policy.bind(window(1), 7, cursor(3));
    policy.clipped(&fake.clip, TRUE);
    fake.clip.right = 99;
    fake.capture = window(4);
    Lose(policy);
    expect(!fake.unclips && !fake.releases,
           "do not release another window's capture or changed clip");
    policy.unbind(window(1));
    expect(fake.count == -3 && !fake.unclips && !fake.releases,
           "teardown preserves external ownership and restores only our debt");
    fake = {};
    tdur::mod::ForegroundCursor failed(Api());
    failed.bind(window(1), 7, cursor(3));
    failed.clipped(&fake.clip, FALSE);
    Lose(failed);
    expect(!fake.unclips, "failed ClipCursor is not an acquired lease");
    Gain(failed);
    failed.clipped(&fake.clip, TRUE);
    failed.clipped(nullptr, TRUE);
    Lose(failed);
    expect(!fake.unclips, "explicit unclip clears our lease");
}
void bounded_and_scope() {
    Reset(-100);
    fake.get_clip_succeeds = false;
    tdur::mod::ForegroundCursor policy(Api(), Captured);
    policy.bind(window(1), 7, cursor(3));
    policy.clipped(&fake.clip, TRUE);
    Lose(policy);
    expect(fake.shows == 100 && !policy.visibility_limit_reached(),
           "a deep host counter becomes visible within the cap");
    expect(LastLine().event == "foreground-cursor-released" &&
               LastLine().reason == "trigger=activation shows=100 count=0 "
                                    "capture=1 clip=0 showing=0/1",
           "release logs its trigger, real shows and the returned counter");
    expect(!fake.unclips, "unknown clipping rectangle is not discarded");
    Gain(policy);
    expect(fake.count == -1 && fake.hides == 1,
           "return gives back a single hide");
    expect(LastLine().event == "foreground-cursor-returned" &&
               LastLine().reason ==
                   "trigger=activation hides=1 count=-1 blocked=0",
           "return logs its trigger, real hides and the returned counter");
    policy.unbind(window(1));
    Reset(-5000);
    tdur::mod::ForegroundCursor broken(Api(), Captured);
    broken.bind(window(1), 7, cursor(3));
    Lose(broken);
    expect(fake.shows == 4096 && broken.visibility_limit_reached(),
           "broken host counter cannot create an unbounded loop");
    expect(LastLine().event == "foreground-cursor-visibility-cap" &&
               LastLine().reason == "trigger=activation shows=4096 count=-904 "
                                    "capture=1 clip=0 showing=0/0",
           "capped release logs its real shows and the returned counter");
    Gain(broken);
    expect(fake.count == -905 && fake.hides == 1,
           "capped return also gives back a single hide");
    broken.unbind(window(1));
    fake = {};
    tdur::mod::ForegroundCursor scoped(Api());
    scoped.bind(window(1), 7, cursor(3));
    tdur::mod::SetActiveForegroundCursor(&scoped);
    TdurForegroundCursorBefore(window(2), WM_ACTIVATEAPP, FALSE);
    TdurForegroundCursorAfter(window(2), WM_ACTIVATEAPP, FALSE);
    expect(!fake.shows && !scoped.background(),
           "another display HWND cannot deactivate the bound window");
    TdurForegroundCursorBefore(window(1), WM_ACTIVATEAPP, FALSE);
    TdurForegroundCursorAfter(window(1), WM_ACTIVATEAPP, FALSE);
    expect(fake.shows == 3, "production message bridge reaches the policy");
    tdur::mod::SetActiveForegroundCursor(nullptr);
    scoped.unbind(window(1));
}
void retail_runaway_regression() {
    fake = {};
    fake.count = 0;
    tdur::mod::ForegroundCursor policy(Api());
    policy.bind(window(1), 7, cursor(3));
    int result{};
    expect(!policy.skip_repeated_retail_hide(result),
           "unknown counter must call the real ShowCursor first");
    // Replay the 2,502 hide requests recorded in the official B trial.
    for (unsigned i = 0; i != 2502; ++i) {
        if (!policy.skip_repeated_retail_hide(result)) {
            result = Show(FALSE);
            policy.observed_show(result);
        }
    }
    expect(
        fake.hides == 1 && fake.count == -1,
        "known retail caller cannot accumulate a negative counter per frame");
    Lose(policy);
    expect(
        fake.count == 0 && fake.shows == 1 &&
            !policy.visibility_limit_reached(),
        "recorded runaway scenario now releases a visible cursor immediately");
    Gain(policy);
    expect(fake.count == -1 && fake.hides == 2,
           "return to game restores one balanced hide, not thousands");
    // A different caller still executes its real operation and is observed.
    policy.observed_show(Show(TRUE));
    expect(!policy.skip_repeated_retail_hide(result) && fake.count == 0,
           "a later real show invalidates the already-hidden shortcut");
    policy.unbind(window(1));
}
// Transcriptions of HookShowCursor (cursor_diagnostics.cpp): intercept()
// first; the retail site keeps its already-hidden shortcut, any other
// caller always reaches the API.
int RetailHide(tdur::mod::ForegroundCursor& policy) {
    int result{};
    if (policy.intercept(true))
        return tdur::mod::ForegroundCursor::background_show_result(FALSE);
    if (policy.skip_repeated_retail_hide(result))
        return result;
    result = Show(FALSE);
    policy.observed_show(result);
    return result;
}
int OtherShow(tdur::mod::ForegroundCursor& policy, BOOL show) {
    if (policy.intercept(true))
        return tdur::mod::ForegroundCursor::background_show_result(show);
    const int result = Show(show);
    policy.observed_show(result);
    return result;
}
struct CycleCheck {
    bool visible_after_loss = true;
    bool no_cap = true;
    bool game_count = true;
};
void LoseAsRecorded(tdur::mod::ForegroundCursor& policy, CycleCheck& check) {
    policy.before_message(WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, 0));
    policy.after_message(WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, 0));
    fake.foreground = window(2);
    policy.before_message(WM_ACTIVATEAPP, FALSE);
    policy.after_message(WM_ACTIVATEAPP, FALSE);
    check.visible_after_loss &= fake.count >= 0;
    check.no_cap &= !policy.visibility_limit_reached();
    for (unsigned i = 0; i != 355; ++i)
        (void)RetailHide(policy);
}
void ActivePeriod(tdur::mod::ForegroundCursor& policy, bool shows_first,
                  unsigned retail_hides, CycleCheck& check) {
    if (shows_first) {
        (void)OtherShow(policy, TRUE);
        (void)OtherShow(policy, TRUE);
    }
    for (unsigned i = 0; i != retail_hides; ++i)
        (void)RetailHide(policy);
    if (!shows_first) {
        (void)OtherShow(policy, TRUE);
        (void)OtherShow(policy, TRUE);
    }
    check.game_count &= fake.count == -1;
}
// Recorded cycle 1: return by WM_ACTIVATEAPP(TRUE) then WM_ACTIVATE(WA_ACTIVE).
void FirstRecordedCycle(tdur::mod::ForegroundCursor& policy, bool shows_first,
                        CycleCheck& check) {
    LoseAsRecorded(policy, check);
    fake.foreground = window(1);
    policy.before_message(WM_ACTIVATEAPP, TRUE);
    (void)OtherShow(policy, FALSE);
    (void)OtherShow(policy, FALSE);
    (void)OtherShow(policy, TRUE);
    (void)OtherShow(policy, TRUE);
    policy.after_message(WM_ACTIVATEAPP, TRUE);
    policy.before_message(WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, 0));
    (void)OtherShow(policy, FALSE);
    (void)OtherShow(policy, FALSE);
    policy.after_message(WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, 0));
    ActivePeriod(policy, shows_first, 49, check);
}
// Recorded cycle 2: return by minimized WM_ACTIVATE, then WM_ACTIVATEAPP(TRUE).
void SecondRecordedCycle(tdur::mod::ForegroundCursor& policy, bool shows_first,
                         CycleCheck& check) {
    LoseAsRecorded(policy, check);
    fake.foreground = window(1);
    policy.before_message(WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, 1));
    (void)RetailHide(policy);
    policy.after_message(WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, 1));
    policy.before_message(WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, 0));
    policy.after_message(WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, 0));
    policy.before_message(WM_ACTIVATEAPP, TRUE);
    (void)OtherShow(policy, FALSE);
    (void)OtherShow(policy, FALSE);
    policy.after_message(WM_ACTIVATEAPP, TRUE);
    ActivePeriod(policy, shows_first, 19, check);
}
// Replays the observed sequences of cycles 1 and 2 of a recorded session;
// it does not prove the origin of the "other" calls, and the placement of the
// two shows of each active period is assumed (both placements are replayed).
void activation_other_module_cycles() {
    for (const unsigned variant : {0U, 1U}) {
        const bool shows_first = variant != 0;
        fake = {};
        fake.count = 0;
        tdur::mod::ForegroundCursor policy(Api());
        policy.bind(window(1), 7, cursor(3));
        // Recorded start-up: 2,502 hides from the retail site and two shows.
        for (unsigned i = 0; i != 2502; ++i) {
            (void)RetailHide(policy);
            if (i == 1 || i == 2)
                (void)OtherShow(policy, TRUE);
        }
        CycleCheck check;
        for (unsigned pair = 0; pair != 20; ++pair) {
            FirstRecordedCycle(policy, shows_first, check);
            SecondRecordedCycle(policy, shows_first, check);
        }
        expect(check.visible_after_loss,
               "recorded replay: cursor visible after every loss");
        expect(check.no_cap, "recorded replay: visibility cap never reached");
        expect(check.game_count,
               "recorded replay: counter -1 at the end of each active period");
        expect(fake.shows == 162 && fake.hides == 163,
               "recorded replay: 162 real shows and 163 real hides");
        std::printf("recorded replay shows_first=%d shows=%u hides=%u "
                    "count=%d\n",
                    shows_first ? 1 : 0, fake.shows, fake.hides, fake.count);
        policy.unbind(window(1));
    }
}
// Constructed case, not observed in the recorded session: a module hides
// twice at each return without a compensating show.
void unbalanced_module_defense() {
    fake = {};
    fake.count = 0;
    tdur::mod::ForegroundCursor policy(Api());
    policy.bind(window(1), 7, cursor(3));
    for (unsigned i = 0; i != 2502; ++i)
        (void)RetailHide(policy);
    CycleCheck check;
    bool game_count = true;
    for (unsigned cycle = 0; cycle != 50; ++cycle) {
        LoseAsRecorded(policy, check);
        fake.foreground = window(1);
        policy.before_message(WM_ACTIVATEAPP, TRUE);
        (void)OtherShow(policy, FALSE);
        (void)OtherShow(policy, FALSE);
        policy.after_message(WM_ACTIVATEAPP, TRUE);
        for (unsigned i = 0; i != 49; ++i)
            (void)RetailHide(policy);
        game_count &= fake.count == -3;
    }
    expect(check.visible_after_loss,
           "unbalanced module: cursor visible after every loss");
    expect(check.no_cap, "unbalanced module: visibility cap never reached");
    expect(game_count, "unbalanced module: counter stable at -3 in game");
    expect(fake.shows == 148 && fake.hides == 151,
           "unbalanced module: 148 real shows and 151 real hides");
    std::printf("unbalanced module shows=%u hides=%u count=%d\n", fake.shows,
                fake.hides, fake.count);
    policy.unbind(window(1));
}
// Foreground lost without any activation message: the next intercepted call
// releases, the return of the foreground takes the pointer back.
void foreground_lost_without_message() {
    Reset(0);
    tdur::mod::ForegroundCursor policy(Api(), Captured);
    policy.bind(window(1), 7, cursor(3));
    policy.clipped(&fake.clip, TRUE);
    (void)RetailHide(policy);
    expect(fake.count == -1 && fake.hides == 1 && !fake.shows &&
               fake.probes == 1,
           "game in front: the game hides, the pointer is read once");
    fake.foreground = window(2);
    (void)RetailHide(policy);
    expect(fake.shows == 1 && fake.count == 0 && fake.releases == 1 &&
               fake.unclips == 1 && fake.sets == 1,
           "foreground lost without message: released at the next call");
    expect(LastLine().event == "foreground-cursor-released" &&
               LastLine().reason ==
                   "trigger=foreground shows=1 count=0 capture=1 clip=1 "
                   "showing=0/1 image=explorer.exe class=Shell_TrayWnd",
           "release names its trigger, what it took back and the window");
    for (unsigned i = 0; i != 5; ++i)
        (void)RetailHide(policy);
    expect(fake.shows == 1 && fake.count == 0 && fake.probes == 1 &&
               released_lines == 1,
           "unfocused: no second release and no pointer read");
    fake.foreground = window(1);
    fake.tick += 1000;
    (void)RetailHide(policy);
    expect(fake.count == -1 && fake.hides == 2 && fake.probes == 2 &&
               !policy.background(),
           "foreground back: a single hide given back, then the shortcut");
    expect(LastLine().event == "foreground-cursor-returned" &&
               LastLine().reason ==
                   "trigger=foreground hides=1 count=-1 blocked=6",
           "return names its trigger and the refused calls");
    fake.thread = 9;
    expect(!policy.intercept(true), "another thread is never refused");
    fake.thread = 7;
    policy.unbind(window(1));
    expect(LastLine().event == "foreground-cursor-unbound" &&
               LastLine().reason ==
                   "hides=0 count=none blocked=6 releases=1 returns=1 "
                   "read_game=2 read_foreign=0 read_cloaked=0 read_hidden=0 "
                   "read_unreadable=0",
           "unbind writes the refused calls, transitions and reads");
    policy.bind(window(1), 7, cursor(3));
    fake.foreground = window(2);
    (void)RetailHide(policy);
    fake.foreground = window(1);
    fake.tick += 1000;
    (void)RetailHide(policy);
    expect(LastLine().reason == "trigger=foreground hides=1 count=-1 blocked=1",
           "a new binding counts its refused calls from zero");
    policy.unbind(window(1));
    expect(LastLine().reason ==
               "hides=0 count=none blocked=1 releases=1 returns=1 read_game=1 "
               "read_foreign=0 read_cloaked=0 read_hidden=0 read_unreadable=0",
           "a new binding counts its transitions and reads from zero");
}
// A window of another process under the pointer while the game keeps the
// Win32 foreground: released, calls refused, taken back once the pointer is
// on the game again (read at most once per 50 ms while it does not move).
void overlay_with_game_in_front() {
    Reset(0);
    tdur::mod::ForegroundCursor policy(Api(), Captured);
    policy.bind(window(1), 7, cursor(3));
    policy.clipped(&fake.clip, TRUE);
    (void)RetailHide(policy);
    fake.under = overlay_part;
    fake.tick += 50;
    (void)RetailHide(policy);
    expect(fake.shows == 1 && fake.count == 0 && fake.releases == 1 &&
               fake.unclips == 1 && fake.probes == 2,
           "overlay under the pointer, game in front: released");
    expect(LastLine().event == "foreground-cursor-released" &&
               LastLine().reason ==
                   "trigger=overlay shows=1 count=0 capture=1 clip=1 "
                   "showing=0/1 image=GameBar.exe class=ApplicationFrameWindow",
           "overlay release names the process image and the root class");
    expect(fake.root_flags == GA_ROOT && fake.point_matched,
           "the root of the window at the pointer position is checked");
    expect(Refused(policy) && Refused(policy) && Refused(policy),
           "warp, clip and capture stay refused while the overlay is in front");
    fake.under = window(1);
    (void)RetailHide(policy);
    fake.tick += 49;
    (void)RetailHide(policy);
    expect(fake.probes == 2 && fake.hides == 1 && policy.background(),
           "unmoved pointer: not read again within 50 ms");
    fake.tick += 1;
    (void)RetailHide(policy);
    expect(fake.probes == 3 && fake.count == -1 && fake.hides == 2 &&
               !policy.background(),
           "pointer back on the game: a single hide at the next read");
    expect(LastLine().event == "foreground-cursor-returned" &&
               LastLine().reason ==
                   "trigger=overlay hides=1 count=-1 blocked=6",
           "return from an overlay names its trigger");
    policy.unbind(window(1));
    // A deep counter left by another module: still a single hide on return.
    fake = {};
    fake.count = -3;
    tdur::mod::ForegroundCursor deep(Api());
    deep.bind(window(1), 7, cursor(3));
    fake.under = overlay;
    deep.poll();
    expect(fake.shows == 3 && fake.count == 0,
           "a deep counter becomes visible under an overlay");
    fake.under = window(1);
    fake.tick += 50;
    deep.poll();
    expect(fake.hides == 1 && fake.count == -1,
           "return from an overlay gives back a single hide");
    deep.unbind(window(1));
}
// Foreign windows that are not in front, windows of the game process, reads
// that cannot decide, and the count of reads by result.
void windows_that_keep_ownership() {
    Reset(0);
    tdur::mod::ForegroundCursor policy(Api(), Captured);
    policy.bind(window(1), 7, cursor(3));
    (void)RetailHide(policy);
    const auto read_at = [](HWND target) {
        fake.under = target;
        fake.tick += 50;
    };
    read_at(overlay);
    fake.overlay_cloaked = 1;
    (void)RetailHide(policy);
    expect(fake.probes == 2 && !fake.shows && !policy.background(),
           "a foreign window cloaked by DWM is not in front");
    fake.overlay_cloaked = 0;
    fake.overlay_visible = FALSE;
    read_at(overlay);
    (void)RetailHide(policy);
    expect(fake.probes == 3 && !fake.shows && !policy.background(),
           "an invisible foreign window is not in front");
    fake.overlay_visible = TRUE;
    read_at(game_child);
    (void)RetailHide(policy);
    read_at(game_popup);
    (void)RetailHide(policy);
    expect(fake.probes == 5 && !fake.shows && !policy.background(),
           "a child or another window of the game process keeps ownership");
    read_at(nullptr);
    (void)RetailHide(policy);
    read_at(lifeless);
    (void)RetailHide(policy);
    expect(fake.probes == 7 && !fake.shows && !policy.background(),
           "no window or no process at the pointer changes nothing");
    read_at(overlay);
    fake.point_fails = true;
    (void)RetailHide(policy);
    expect(fake.probes == 7 && !fake.shows,
           "an unreadable pointer position changes nothing");
    fake.point_fails = false;
    read_at(overlay);
    (void)RetailHide(policy);
    expect(fake.shows == 1 && policy.background(),
           "the overlay is seen at the next readable position");
    fake.point_fails = true;
    read_at(window(1));
    (void)RetailHide(policy);
    expect(fake.hides == 1 && policy.background(),
           "an unreadable position does not take the pointer back");
    fake.point_fails = false;
    read_at(window(1));
    (void)RetailHide(policy);
    expect(fake.hides == 2 && !policy.background(),
           "a readable position on the game takes it back");
    fake.attribute_fails = true;
    read_at(overlay);
    (void)RetailHide(policy);
    expect(fake.shows == 2 && policy.background(),
           "a failed DWM query does not cloak a foreign window");
    fake.attribute_fails = false;
    read_at(window(1));
    (void)RetailHide(policy);
    read_at(rootless);
    (void)RetailHide(policy);
    expect(fake.shows == 3 &&
               LastLine().reason ==
                   "trigger=overlay shows=1 count=0 capture=0 clip=0 "
                   "showing=0/1 image=GameBar.exe class=RootlessWindow",
           "a window without root is checked itself");
    policy.unbind(window(1));
    expect(LastLine().reason ==
               "hides=1 count=-1 blocked=4 releases=3 returns=2 read_game=5 "
               "read_foreign=3 read_cloaked=1 read_hidden=1 read_unreadable=4",
           "unbind counts the reads by result");
}
// The foreground WinEvent: installed at bind for every process, out of
// context; its callback has the effect of an intercepted call, the pointer
// being read at once.
void foreground_event() {
    Reset(0);
    tdur::mod::ForegroundCursor policy(Api(), Captured);
    policy.bind(window(1), 7, cursor(3));
    expect(
        fake.hooks == 1 && fake.hook_min == EVENT_SYSTEM_FOREGROUND &&
            fake.hook_max == EVENT_SYSTEM_FOREGROUND && !fake.hook_module &&
            fake.hook_proc && !fake.hook_process && !fake.hook_thread &&
            fake.hook_flags == WINEVENT_OUTOFCONTEXT,
        "bind installs one out-of-context foreground hook for all processes");
    expect(LastLine().event == "foreground-cursor-bound" &&
               LastLine().reason == "event_hook=1",
           "bind logs the hook installation");
    tdur::mod::SetActiveForegroundCursor(&policy);
    (void)RetailHide(policy);
    Event();
    expect(fake.probes == 2 && !fake.shows,
           "a foreground event reads the pointer at once, game kept in front");
    fake.foreground = window(2);
    Event();
    expect(fake.shows == 1 && fake.count == 0 &&
               LastLine().reason ==
                   "trigger=foreground shows=1 count=0 capture=1 clip=0 "
                   "showing=0/1 image=explorer.exe class=Shell_TrayWnd",
           "a foreground event alone releases the pointer");
    fake.foreground = window(1);
    Event();
    expect(fake.hides == 2 && fake.count == -1 &&
               LastLine().reason ==
                   "trigger=foreground hides=1 count=-1 blocked=0",
           "a foreground event alone takes the pointer back");
    fake.under = overlay;
    Event();
    expect(fake.probes == 4 && fake.shows == 2 &&
               LastLine().reason ==
                   "trigger=overlay shows=1 count=0 capture=0 clip=0 "
                   "showing=0/1 image=GameBar.exe class=ApplicationFrameWindow",
           "a foreground event reads the pointer even within 50 ms");
    fake.under = window(1);
    Event(reinterpret_cast<HWINEVENTHOOK>(static_cast<UINT_PTR>(0x5A5B)));
    Event(nullptr, EVENT_OBJECT_FOCUS);
    fake.thread = 9;
    Event();
    fake.thread = 7;
    expect(fake.probes == 4 && fake.hides == 2,
           "events of another hook, kind or thread are ignored");
    Event();
    expect(fake.probes == 5 && fake.hides == 3 &&
               LastLine().reason ==
                   "trigger=overlay hides=1 count=-1 blocked=0",
           "the next foreground event takes the pointer back");
    policy.unbind(window(1));
    expect(fake.unhooks == 1 && fake.unhooked == fake.hook_result,
           "unbind removes the hook it installed");
    Event();
    expect(fake.probes == 5, "no event reaches an unbound policy");
    tdur::mod::SetActiveForegroundCursor(nullptr);
    Event();
    expect(fake.probes == 5, "an event without an installed policy is ignored");
    Reset(0);
    fake.hook_result = nullptr;
    tdur::mod::ForegroundCursor unhooked(Api(), Captured);
    unhooked.bind(window(1), 7, cursor(3));
    expect(LastLine().event == "foreground-cursor-bound" &&
               LastLine().reason == "event_hook=0",
           "a failed hook installation is logged");
    tdur::mod::SetActiveForegroundCursor(&unhooked);
    fake.foreground = window(2);
    Event();
    expect(!fake.shows, "a null hook handle reaches nothing");
    unhooked.unbind(window(1));
    expect(!fake.unhooks, "a hook never installed is not removed");
    tdur::mod::SetActiveForegroundCursor(nullptr);
}
// The window under the pointer is read at most once per 50 ms, and never
// while the foreground is elsewhere.
void pointer_read_cadence() {
    fake = {};
    tdur::mod::ForegroundCursor policy(Api());
    policy.bind(window(1), 7, cursor(3));
    for (unsigned i = 0; i != 10; ++i)
        policy.poll();
    expect(fake.probes == 1, "ten calls at one instant read the pointer once");
    fake.tick += 49;
    for (unsigned i = 0; i != 10; ++i)
        policy.poll();
    expect(fake.probes == 1, "no second read within 50 ms");
    fake.tick += 1;
    for (unsigned i = 0; i != 10; ++i)
        policy.poll();
    expect(fake.probes == 2, "one read once 50 ms have passed");
    fake.foreground = window(2);
    fake.tick += 1000;
    for (unsigned i = 0; i != 10; ++i)
        policy.poll();
    expect(fake.probes == 2 && fake.shows == 3,
           "no read while the foreground is elsewhere");
    fake.foreground = window(1);
    policy.poll();
    expect(fake.probes == 3 && fake.hides == 1 && fake.count == -1,
           "the pointer is read when the foreground comes back");
    policy.unbind(window(1));
}
// While an overlay holds the release, a moved pointer is read at once.
void moved_pointer_read() {
    Reset(-1);
    tdur::mod::ForegroundCursor policy(Api(), Captured);
    policy.bind(window(1), 7, cursor(3));
    fake.under = overlay;
    policy.poll();
    fake.point.x += 5;
    policy.poll();
    expect(fake.probes == 2 && policy.background(),
           "a horizontal move over the overlay is read at once");
    policy.poll();
    expect(fake.probes == 2, "an unmoved pointer waits for the interval");
    fake.point_fails = true;
    policy.poll();
    expect(fake.probes == 2 && policy.background(),
           "an unreadable position starts no early read");
    fake.point_fails = false;
    fake.under = window(1);
    fake.point.y += 5;
    policy.poll();
    expect(fake.probes == 3 && fake.hides == 1 && !policy.background(),
           "a vertical move back on the game takes the pointer back at once");
    fake.point.x += 5;
    policy.poll();
    expect(fake.probes == 3, "without an overlay release, a move waits");
    fake.foreground = window(2);
    policy.poll();
    fake.foreground = window(1);
    fake.point.x += 5;
    policy.poll();
    expect(fake.probes == 3 && fake.hides == 2,
           "a release by the foreground ends without reading the pointer");
    policy.unbind(window(1));
    expect(LastLine().reason ==
               "hides=0 count=none blocked=0 releases=2 returns=2 read_game=1 "
               "read_foreign=2 read_cloaked=0 read_hidden=0 read_unreadable=0",
           "early reads are made only for a readable moved pointer");
}
// WM_SETCURSOR reaching the bound window ends an overlay release at once,
// while the game is in front.
void cursor_message_return() {
    Reset(-1);
    tdur::mod::ForegroundCursor policy(Api(), Captured);
    policy.bind(window(1), 7, cursor(3));
    fake.under = overlay;
    policy.poll();
    fake.under = window(1);
    policy.before_message(WM_MOUSEMOVE, 0);
    expect(!fake.hides && policy.background(),
           "another message does not end the overlay release");
    fake.foreground = window(2);
    policy.before_message(WM_SETCURSOR, 0);
    expect(!fake.hides, "WM_SETCURSOR without the foreground gives nothing");
    fake.foreground = window(1);
    policy.before_message(WM_SETCURSOR, 0);
    policy.after_message(WM_SETCURSOR, 0);
    expect(fake.hides == 1 && fake.count == -1 && fake.probes == 1 &&
               !policy.background() &&
               LastLine().reason ==
                   "trigger=overlay hides=1 count=-1 blocked=0",
           "WM_SETCURSOR on the bound window takes the pointer back at once");
    policy.poll();
    expect(fake.shows == 1 && fake.probes == 1,
           "the next call keeps the pointer with the game");
    fake.foreground = window(2);
    policy.poll();
    fake.foreground = window(1);
    policy.before_message(WM_SETCURSOR, 0);
    expect(fake.hides == 1, "WM_SETCURSOR does not end a release by the "
                            "foreground");
    policy.poll();
    expect(fake.hides == 2, "the next call does");
    fake.under = overlay;
    fake.tick += 50;
    policy.poll();
    policy.before_message(WM_ACTIVATEAPP, FALSE);
    policy.after_message(WM_ACTIVATEAPP, FALSE);
    policy.before_message(WM_SETCURSOR, 0);
    expect(fake.hides == 2 && policy.background(),
           "during a deactivation, WM_SETCURSOR leaves the return to the "
           "message");
    policy.before_message(WM_ACTIVATEAPP, TRUE);
    expect(fake.hides == 3 && !policy.background(),
           "the return message gives the pointer back");
    policy.unbind(window(1));
}
// The release line tells what was taken back and whether the cursor showed.
void release_line_fields() {
    Reset(0);
    tdur::mod::ForegroundCursor policy(Api(), Captured);
    policy.bind(window(1), 7, cursor(3));
    policy.clipped(&fake.clip, TRUE);
    fake.clip.right = 99;
    fake.capture = window(4);
    fake.foreground = window(2);
    policy.poll();
    expect(LastLine().reason ==
               "trigger=foreground shows=1 count=1 capture=0 clip=0 "
               "showing=1/1 image=explorer.exe class=Shell_TrayWnd",
           "neither capture nor clip of another owner; cursor already shown");
    fake.foreground = window(1);
    policy.poll();
    fake.cursor_info_fails = true;
    fake.foreground = window(2);
    policy.poll();
    expect(LastLine().reason ==
               "trigger=foreground shows=1 count=1 capture=0 clip=0 "
               "showing=-/- image=explorer.exe class=Shell_TrayWnd",
           "an unreadable cursor state is marked");
    policy.unbind(window(1));
}
// The first 64 releases and returns are written in full, then one line at
// the switch; unbind gives the totals.
void transition_lines() {
    Reset(-1);
    tdur::mod::ForegroundCursor policy(Api(), Captured);
    policy.bind(window(1), 7, cursor(3));
    for (unsigned cycle = 0; cycle != 40; ++cycle) {
        Lose(policy);
        Gain(policy);
    }
    expect(released_lines == 32 && returned_lines == 32 && capped_lines == 1,
           "64 transitions written, then one line at the switch");
    expect(fake.count == -1 && fake.shows == 40 && fake.hides == 40,
           "transitions beyond the lines keep their effect");
    policy.unbind(window(1));
    expect(LastLine().event == "foreground-cursor-unbound" &&
               LastLine().reason ==
                   "hides=0 count=none blocked=0 releases=40 returns=40 "
                   "read_game=0 read_foreign=0 read_cloaked=0 read_hidden=0 "
                   "read_unreadable=0",
           "unbind gives the totals beyond the lines written");
    policy.bind(window(1), 7, cursor(3));
    Lose(policy);
    expect(released_lines == 32 && capped_lines == 1,
           "the line budget is the policy's, not the binding's");
    policy.unbind(window(1));
}
// A call that is never refused (ClipCursor without a rectangle,
// ReleaseCapture) refreshes ownership but is neither refused nor counted.
void refusable_calls() {
    Reset(-1);
    tdur::mod::ForegroundCursor policy(Api(), Captured);
    policy.bind(window(1), 7, cursor(3));
    fake.foreground = window(2);
    expect(!policy.intercept(false) && fake.shows == 1,
           "an unrefusable call refreshes ownership and is let through");
    expect(policy.intercept(true), "a refusable call is refused");
    policy.unbind(window(1));
    expect(LastLine().reason ==
               "hides=1 count=-1 blocked=1 releases=1 returns=0 read_game=0 "
               "read_foreign=0 read_cloaked=0 read_hidden=0 read_unreadable=0",
           "only the refused call is counted");
}
// An activation message without a deactivation before it also forgets the
// overlay seen before.
void activation_clears_overlay() {
    Reset(-1);
    tdur::mod::ForegroundCursor policy(Api());
    policy.bind(window(1), 7, cursor(3));
    fake.under = overlay;
    policy.poll();
    policy.before_message(WM_ACTIVATE, WA_CLICKACTIVE);
    policy.after_message(WM_ACTIVATE, WA_CLICKACTIVE);
    policy.poll();
    expect(fake.shows == 1 && fake.hides == 1 && !policy.background(),
           "an activation return is not undone by a stale overlay");
    policy.unbind(window(1));
}
// A button held down may be a drag the game captured the mouse for: no
// overlay release before it is let go.
void drag_keeps_capture() {
    Reset(-1);
    tdur::mod::ForegroundCursor policy(Api());
    policy.bind(window(1), 7, cursor(3));
    fake.under = overlay;
    for (const int button : {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON}) {
        fake.key_down = button;
        fake.tick += 50;
        policy.poll();
    }
    expect(fake.probes == 3 && !fake.shows && fake.releases == 0 &&
               !policy.background(),
           "left, right or middle button held: no overlay release");
    fake.key_down = 0;
    fake.tick += 50;
    policy.poll();
    expect(fake.shows == 1 && fake.releases == 1 && policy.background(),
           "buttons let go: the overlay releases");
    fake.key_down = VK_LBUTTON;
    fake.tick += 50;
    policy.poll();
    expect(fake.hides == 0 && policy.background(),
           "a button pressed on the overlay keeps the release");
    policy.unbind(window(1));
}
// A foreground event that finds the game in front ends a deactivation whose
// return message never came.
void deactivation_safety_net() {
    Reset(-1);
    tdur::mod::ForegroundCursor policy(Api(), Captured);
    policy.bind(window(1), 7, cursor(3));
    tdur::mod::SetActiveForegroundCursor(&policy);
    Lose(policy);
    Event();
    expect(!fake.hides && policy.background(),
           "a foreground event elsewhere leaves the deactivation");
    fake.foreground = window(1);
    fake.tick += 1000;
    policy.poll();
    expect(!fake.hides && !fake.probes && policy.background(),
           "calls stay inert until the return message or a foreground event");
    Event();
    expect(fake.hides == 1 && !policy.background() &&
               LastLine().reason ==
                   "trigger=foreground hides=1 count=-1 blocked=0",
           "a foreground event with the game in front takes the pointer back");
    policy.before_message(WM_ACTIVATEAPP, TRUE);
    policy.after_message(WM_ACTIVATEAPP, TRUE);
    expect(fake.hides == 1, "a late return message gives nothing back twice");
    tdur::mod::SetActiveForegroundCursor(nullptr);
    policy.unbind(window(1));
}
// Logged names: only listed shell components, by file name and case
// insensitive; any other process is "other". Never a coordinate or handle.
void log_names() {
    Reset(-1);
    fake.overlay_image = L"X:/fixture/apps/gamebar.exe";
    fake.overlay_class =
        L"Overlay Class=Name\x00e9\"/\\Beyond48CharactersOfAClassNameIsCut";
    tdur::mod::ForegroundCursor policy(Api(), Captured);
    policy.bind(window(1), 7, cursor(3));
    const auto cycle = [&policy](const wchar_t* image) {
        fake.overlay_image = image;
        fake.under = overlay;
        fake.tick += 50;
        policy.poll();
        const std::string reason = LastLine().reason;
        fake.under = window(1);
        fake.tick += 50;
        policy.poll();
        return reason;
    };
    expect(cycle(L"X:/fixture/apps/gamebar.exe") ==
               "trigger=overlay shows=1 count=0 capture=1 clip=0 showing=0/1 "
               "image=GameBar.exe "
               "class=Overlay_Class_Name____Beyond48CharactersOfAClass",
           "a listed component: its file name, the class in bounded ASCII");
    expect(cycle(L"X:\\fixture\\apps\\Browser.exe") ==
               "trigger=overlay shows=1 count=0 capture=0 clip=0 showing=0/1 "
               "image=other class=other",
           "any other process is not named");
    expect(cycle(L"X:\\fixture\\apps\\GameBar.exe2") ==
                   "trigger=overlay shows=1 count=0 capture=0 clip=0 "
                   "showing=0/1 image=other class=other" &&
               cycle(L"X:\\fixture\\apps\\GameBar.ex") ==
                   "trigger=overlay shows=1 count=0 capture=0 clip=0 "
                   "showing=0/1 image=other class=other",
           "a listed name must match exactly");
    expect(cycle(nullptr) == "trigger=overlay shows=1 count=0 capture=0 "
                             "clip=0 showing=0/1 image=unknown class=unknown",
           "an unreadable image names nothing");
    fake.overlay_class = nullptr;
    expect(cycle(L"X:\\fixture\\apps\\GameBar.exe") ==
               "trigger=overlay shows=1 count=0 capture=0 clip=0 showing=0/1 "
               "image=GameBar.exe class=unknown",
           "an unreadable class of a listed component is marked");
    fake.foreground = game_popup;
    policy.poll();
    expect(LastLine().reason ==
               "trigger=foreground shows=1 count=0 capture=0 clip=0 "
               "showing=0/1 image=other class=other",
           "the game's own other window is not named either");
    fake.foreground = window(1);
    fake.tick += 50;
    policy.poll();
    fake.foreground = nullptr;
    policy.poll();
    expect(LastLine().reason ==
               "trigger=foreground shows=1 count=0 capture=0 clip=0 "
               "showing=0/1 image=none class=none",
           "a foreground lost to no window names none");
    fake.foreground = window(1);
    fake.tick += 50;
    policy.poll();
    policy.unbind(window(1));
    expect(line_count == 18 && !leaked,
           "no pointer coordinate or handle is ever logged");
}
// ReleaseCapture sends WM_CAPTURECHANGED to the game, whose handler can make
// an intercepted call before a release or a teardown is complete.
void reentrant_calls() {
    Reset(-1);
    tdur::mod::ForegroundCursor policy(Api(), Captured);
    policy.bind(window(1), 7, cursor(3));
    fake.under = overlay;
    fake.reenter = &policy;
    fake.reenter_under = window(1);
    policy.poll();
    expect(fake.releases == 1 && fake.shows == 1 && fake.count == 0 &&
               !fake.hides && released_lines == 1 && !returned_lines &&
               policy.background(),
           "a call made during a release does not take the pointer back");
    fake.tick += 50;
    policy.poll();
    expect(fake.hides == 1 && fake.count == -1 && returned_lines == 1,
           "the next call takes the pointer back once");
    fake.capture = window(1);
    fake.reenter = &policy;
    fake.reenter_under = overlay;
    policy.unbind(window(1));
    expect(fake.releases == 2 && fake.shows == 1 && released_lines == 1 &&
               fake.count == -1,
           "a call made during unbind does not release again");
}
// The activation messages keep their effect alongside the polling.
void activation_messages_and_polling() {
    Reset(-1);
    tdur::mod::ForegroundCursor policy(Api(), Captured);
    policy.bind(window(1), 7, cursor(3));
    fake.foreground = window(2);
    policy.poll();
    policy.before_message(WM_ACTIVATEAPP, FALSE);
    policy.after_message(WM_ACTIVATEAPP, FALSE);
    expect(fake.shows == 1 && released_lines == 1,
           "deactivation after a release by call does not release twice");
    fake.foreground = window(1);
    fake.tick += 1000;
    policy.poll();
    expect(!fake.hides && !fake.probes && policy.background(),
           "while a deactivation holds the release, calls do not take back");
    policy.before_message(WM_ACTIVATEAPP, TRUE);
    policy.after_message(WM_ACTIVATEAPP, TRUE);
    expect(fake.hides == 1 && fake.count == -1 &&
               LastLine().reason ==
                   "trigger=activation hides=1 count=-1 blocked=0",
           "the activation message takes the pointer back");
    fake.under = overlay;
    fake.tick += 50;
    policy.poll();
    policy.before_message(WM_ACTIVATE, WA_INACTIVE);
    fake.foreground = window(2);
    policy.after_message(WM_ACTIVATE, WA_INACTIVE);
    expect(fake.shows == 2 && released_lines == 2,
           "deactivation after an overlay release does not release twice");
    fake.foreground = window(1);
    fake.under = window(1);
    policy.before_message(WM_ACTIVATE, WA_ACTIVE);
    policy.after_message(WM_ACTIVATE, WA_ACTIVE);
    policy.poll();
    expect(fake.hides == 2 && fake.shows == 2 && fake.probes == 1 &&
               !policy.background(),
           "an overlay seen before a deactivation is not reused after it");
    policy.unbind(window(1));
}
} // namespace

int main() {
    ownership();
    alt_tab_balance();
    external_ownership();
    bounded_and_scope();
    retail_runaway_regression();
    activation_other_module_cycles();
    unbalanced_module_defense();
    foreground_lost_without_message();
    overlay_with_game_in_front();
    windows_that_keep_ownership();
    foreground_event();
    pointer_read_cadence();
    moved_pointer_read();
    cursor_message_return();
    release_line_fields();
    transition_lines();
    refusable_calls();
    activation_clears_overlay();
    drag_keeps_capture();
    deactivation_safety_net();
    log_names();
    reentrant_calls();
    activation_messages_and_polling();
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
