// No desktop mouse, visibility counter, clipping, focus or capture is changed.
#include "tdur/mod/foreground_cursor.hpp"

#include <cstdio>
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
// The last 16 policy log lines, event and reason.
struct Line {
    std::string event, reason;
};
Line lines[16];
unsigned line_count{};
void Captured(const char* event, const char* reason) noexcept {
    lines[line_count % 16] = {event, reason ? reason : ""};
    ++line_count;
}
const Line& LastLine() { return lines[(line_count + 15) % 16]; }
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
    fake = {};
    fake.count = -100;
    fake.get_clip_succeeds = false;
    line_count = 0;
    tdur::mod::ForegroundCursor policy(Api(), Captured);
    policy.bind(window(1), 7, cursor(3));
    policy.clipped(&fake.clip, TRUE);
    Lose(policy);
    expect(fake.shows == 100 && !policy.visibility_limit_reached(),
           "a deep host counter becomes visible within the cap");
    expect(LastLine().event == "foreground-cursor-released" &&
               LastLine().reason == "shows=100 count=0",
           "release logs its real shows and the returned counter");
    expect(!fake.unclips, "unknown clipping rectangle is not discarded");
    Gain(policy);
    expect(fake.count == -1 && fake.hides == 1,
           "return gives back a single hide");
    expect(LastLine().event == "foreground-cursor-returned" &&
               LastLine().reason == "hides=1 count=-1",
           "return logs its real hides and the returned counter");
    policy.unbind(window(1));
    fake = {};
    fake.count = -5000;
    line_count = 0;
    tdur::mod::ForegroundCursor broken(Api(), Captured);
    broken.bind(window(1), 7, cursor(3));
    Lose(broken);
    expect(fake.shows == 4096 && broken.visibility_limit_reached(),
           "broken host counter cannot create an unbounded loop");
    expect(LastLine().event == "foreground-cursor-visibility-cap" &&
               LastLine().reason == "shows=4096 count=-904",
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
// Transcriptions of HookShowCursor (cursor_diagnostics.cpp): the retail site
// keeps its already-hidden shortcut, any other caller always reaches the API.
int RetailHide(tdur::mod::ForegroundCursor& policy) {
    int result{};
    if (policy.background())
        return tdur::mod::ForegroundCursor::background_show_result(FALSE);
    if (policy.skip_repeated_retail_hide(result))
        return result;
    result = Show(FALSE);
    policy.observed_show(result);
    return result;
}
int OtherShow(tdur::mod::ForegroundCursor& policy, BOOL show) {
    if (policy.background())
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
} // namespace

int main() {
    ownership();
    alt_tab_balance();
    external_ownership();
    bounded_and_scope();
    retail_runaway_regression();
    activation_other_module_cycles();
    unbalanced_module_defense();
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
