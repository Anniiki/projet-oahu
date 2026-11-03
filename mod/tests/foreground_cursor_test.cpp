// No desktop mouse, visibility counter, clipping, focus or capture is changed.
#include "tdur/mod/foreground_cursor.hpp"

#include <cstdio>

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
struct Fake {
    DWORD thread = 7;
    HWND foreground = window(1);
    HWND capture = window(1);
    HCURSOR cursor = ::cursor(2);
    RECT clip{5, 10, 15, 20};
    int count = -3;
    unsigned shows{}, hides{}, releases{}, unclips{}, sets{};
    bool child{}, get_clip_succeeds = true;
} fake;
DWORD WINAPI CurrentThread() { return fake.thread; }
HWND WINAPI Foreground() { return fake.foreground; }
BOOL WINAPI Child(HWND, HWND) { return fake.child; }
HWND WINAPI Capture() { return fake.capture; }
BOOL WINAPI Release() {
    ++fake.releases;
    fake.capture = nullptr;
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
auto Api() {
    return tdur::mod::ForegroundCursorApi{CurrentThread, Foreground, Child,
                                          Capture,       Release,    Clip,
                                          GetClip,       Show,       Set};
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
    expect(fake.count == -3 && fake.hides == 3 &&
               !policy.visibility_adjustments() && !policy.background(),
           "restore our balance before the native activation procedure");
    Gain(policy);
    expect(fake.hides == 3, "duplicate activation does not restore twice");
    for (unsigned i = 0; i != 4; ++i) {
        Lose(policy);
        Gain(policy);
    }
    expect(fake.count == -3 && fake.shows == fake.hides,
           "repeated Alt+Tab cycles preserve the original counter");
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
    fake = {};
    fake.count = -100;
    fake.get_clip_succeeds = false;
    tdur::mod::ForegroundCursor policy(Api());
    policy.bind(window(1), 7, cursor(3));
    policy.clipped(&fake.clip, TRUE);
    Lose(policy);
    expect(fake.shows == 32 && policy.visibility_limit_reached(),
           "broken host counter cannot create an unbounded loop");
    expect(!fake.unclips, "unknown clipping rectangle is not discarded");
    Gain(policy);
    expect(fake.count == -100 && fake.hides == 32,
           "even capped visibility restoration remains balanced");
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
} // namespace

int main() {
    ownership();
    alt_tab_balance();
    external_ownership();
    bounded_and_scope();
    retail_runaway_regression();
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
