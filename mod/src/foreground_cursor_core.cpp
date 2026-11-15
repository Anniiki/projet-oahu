#include "tdur/mod/foreground_cursor.hpp"

#include <dwmapi.h>

#include <cstdio>
#include <initializer_list>
#include <iterator>

namespace tdur::mod {
namespace {
std::atomic<ForegroundCursor*> installed{nullptr};
// Bound for the real shows of one focus loss, even with a broken counter.
constexpr unsigned visibility_cap = 4096;
// The window under the pointer is read at most once per interval, except
// on a foreground event and, while an overlay holds the release, once the
// pointer has moved.
constexpr ULONGLONG read_interval_ms = 50;
// Release and return lines written in full; later ones are only counted.
constexpr unsigned detailed_transitions = 64;
// Shell components named in the log; any other process is "other".
constexpr const char* shell_images[] = {
    "GameBar.exe",    "GameBarFTServer.exe",     "StartMenuExperienceHost.exe",
    "SearchHost.exe", "ShellExperienceHost.exe", "ShellHost.exe",
    "explorer.exe",   "TextInputHost.exe",       "LockApp.exe"};
bool inactive(UINT message, WPARAM parameter) noexcept {
    return (message == WM_ACTIVATEAPP && !parameter) ||
           (message == WM_ACTIVATE && LOWORD(parameter) == WA_INACTIVE);
}
bool active(UINT message, WPARAM parameter) noexcept {
    return (message == WM_ACTIVATEAPP && parameter) ||
           (message == WM_ACTIVATE && LOWORD(parameter) != WA_INACTIVE);
}
bool equal(const RECT& a, const RECT& b) noexcept {
    return a.left == b.left && a.top == b.top && a.right == b.right &&
           a.bottom == b.bottom;
}
wchar_t lower(wchar_t c) noexcept {
    return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c - L'A' + L'a') : c;
}
// The listed shell image that a file name designates, case ignored.
const char* shell_image(const wchar_t* name, size_t length) noexcept {
    for (const char* image : shell_images) {
        size_t i = 0;
        while (i != length && image[i] &&
               lower(name[i]) == lower(static_cast<wchar_t>(image[i])))
            ++i;
        if (i == length && !image[i])
            return image;
    }
    return nullptr;
}
// Printable ASCII without the separators of the log format (space, '=',
// quote, slashes); anything else becomes '_'. Bounded, "unknown" if empty.
template <size_t N>
void ascii(const wchar_t* text, size_t length, char (&output)[N]) noexcept {
    size_t used = 0;
    for (; used != length && used + 1 != N; ++used) {
        const wchar_t c = text[used];
        const bool plain = c > L' ' && c < 0x7F && c != L'=' && c != L'"' &&
                           c != L'/' && c != L'\\';
        output[used] = plain ? static_cast<char>(c) : '_';
    }
    output[used] = '\0';
    if (!used)
        std::snprintf(output, N, "unknown");
}
} // namespace

bool ForegroundCursor::owner() const noexcept {
    return window_ && api_.current_thread() == thread_;
}

bool ForegroundCursor::focused(HWND foreground) const noexcept {
    return foreground == window_ || api_.is_child(window_, foreground);
}

bool ForegroundCursor::background() const noexcept {
    if (!owner())
        return false;
    return deactivating_ || overlay_ != nullptr || !focused(api_.foreground());
}

void ForegroundCursor::bind(HWND window, DWORD thread, HCURSOR arrow) noexcept {
    // Display registration and teardown run on the HWND thread. Additional
    // devices for the same HWND do not reset the visibility balance.
    if (window_ == window)
        return;
    if (window_ || !window || api_.current_thread() != thread)
        return;
    arrow_ = arrow;
    process_ = api_.current_process();
    thread_ = thread;
    window_ = window;
    // Out of context, so Windows calls back on this thread from its message
    // loop; every process, so that a foreground change anywhere is seen.
    event_hook_ = api_.event_hook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
        &ForegroundCursor::foreground_event, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (log_)
        log_("foreground-cursor-bound",
             event_hook_ ? "event_hook=1" : "event_hook=0");
}

void CALLBACK ForegroundCursor::foreground_event(HWINEVENTHOOK hook,
                                                 DWORD event, HWND, LONG, LONG,
                                                 DWORD, DWORD) noexcept {
    // Delivered on the thread that installed the hook, from the game's
    // message loop, whose last error is kept; refresh checks ownership.
    auto* cursor = installed.load(std::memory_order_acquire);
    if (!cursor || !hook || hook != cursor->event_hook_ ||
        event != EVENT_SYSTEM_FOREGROUND)
        return;
    const DWORD error = GetLastError();
    cursor->refresh(true);
    SetLastError(error);
}

const char* ForegroundCursor::name(Trigger trigger) noexcept {
    switch (trigger) {
    case Trigger::foreground:
        return "foreground";
    case Trigger::overlay:
        return "overlay";
    case Trigger::activation:
        break;
    }
    return "activation";
}

ForegroundCursor::CountText
ForegroundCursor::counted(unsigned calls) const noexcept {
    // count_ is the value returned by our own last call when calls != 0.
    CountText count{};
    if (calls)
        std::snprintf(count.text, sizeof(count.text), "%d", count_);
    else
        std::snprintf(count.text, sizeof(count.text), "none");
    return count;
}

ForegroundCursor::Names ForegroundCursor::describe(HWND window) const noexcept {
    Names names{};
    if (!window) {
        std::snprintf(names.image, sizeof(names.image), "none");
        std::snprintf(names.window_class, sizeof(names.window_class), "none");
        return names;
    }
    wchar_t path[512]{};
    DWORD process = 0;
    DWORD length = 0;
    if (api_.window_process(window, &process))
        length = api_.process_image(process, path,
                                    static_cast<DWORD>(std::size(path)));
    if (length >= std::size(path))
        length = 0;
    DWORD leaf = 0;
    for (DWORD i = 0; i != length; ++i)
        if (path[i] == L'\\' || path[i] == L'/')
            leaf = i + 1;
    // Only a listed shell component is named, by its file name alone.
    const char* image =
        length ? shell_image(path + leaf, length - leaf) : nullptr;
    if (!image) {
        const char* label = length ? "other" : "unknown";
        std::snprintf(names.image, sizeof(names.image), "%s", label);
        std::snprintf(names.window_class, sizeof(names.window_class), "%s",
                      label);
        return names;
    }
    std::snprintf(names.image, sizeof(names.image), "%s", image);
    wchar_t window_class[257]{};
    const int count = api_.class_name(
        window, window_class, static_cast<int>(std::size(window_class)));
    const bool readable =
        count > 0 && count < static_cast<int>(std::size(window_class));
    ascii(window_class, readable ? static_cast<size_t>(count) : 0,
          names.window_class);
    return names;
}

ForegroundCursor::Pointer
ForegroundCursor::read_window(POINT point, HWND& foreign) const noexcept {
    // No window at the point, or one gone meanwhile, has no process.
    const HWND hit = api_.window_from_point(point);
    HWND root = api_.ancestor(hit, GA_ROOT);
    if (!root)
        root = hit;
    DWORD process = 0;
    if (!api_.window_process(root, &process))
        return Pointer::unreadable;
    if (process == process_)
        return Pointer::game;
    // Hidden and cloaked windows (closed shell surfaces) are not in front.
    if (!api_.visible(root))
        return Pointer::hidden;
    DWORD cloaked = 0;
    if (SUCCEEDED(api_.window_attribute(root, DWMWA_CLOAKED, &cloaked,
                                        static_cast<DWORD>(sizeof(cloaked)))) &&
        cloaked)
        return Pointer::cloaked;
    foreign = root;
    return Pointer::foreign;
}

bool ForegroundCursor::buttons_down() const noexcept {
    // High bit: the physical button is down at this moment.
    for (const int button : {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON})
        if (api_.key_state(button) & 0x8000)
            return true;
    return false;
}

char ForegroundCursor::showing() const noexcept {
    // '1' when the cursor shows, '0' when it does not, '-' when unreadable.
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (!api_.cursor_info(&info))
        return '-';
    return (info.flags & CURSOR_SHOWING) ? '1' : '0';
}

void ForegroundCursor::poll() noexcept { refresh(false); }

bool ForegroundCursor::intercept(bool refusable) noexcept {
    refresh(false);
    if (!refusable || !background())
        return false;
    ++blocked_calls_;
    return true;
}

void ForegroundCursor::refresh(bool event) noexcept {
    // While a deactivation message holds the release, the message path keeps
    // its effect: no read, no release, no return.
    if (!owner() || busy_ || (deactivating_ && !event))
        return;
    const HWND foreground = api_.foreground();
    const bool lost = !focused(foreground);
    if (deactivating_) {
        // A foreground event that finds the game in front ends a
        // deactivation whose return message never came; a late message then
        // finds nothing to give back.
        if (lost)
            return;
        deactivating_ = false;
        foreground_lost_ = true;
    }
    busy_ = true;
    if (!lost)
        read(event);
    // Only a change of ownership releases or takes back.
    const bool owned = !lost && overlay_ == nullptr;
    if (!released_ && !owned)
        release(lost ? Trigger::foreground : Trigger::overlay,
                lost ? foreground : overlay_);
    else if (released_ && owned)
        take_back(foreground_lost_ ? Trigger::foreground : Trigger::overlay);
    foreground_lost_ = lost;
    busy_ = false;
}

void ForegroundCursor::read(bool event) noexcept {
    const ULONGLONG now = api_.tick();
    bool due = event || !pointer_read_ || now - read_tick_ >= read_interval_ms;
    POINT point{};
    bool located = false;
    if (!due && released_ && overlay_ != nullptr) {
        // While an overlay holds the release, a moved pointer is read at once.
        located = api_.cursor_position(&point) != FALSE;
        due = located && (point.x != point_.x || point.y != point_.y);
    }
    if (!due)
        return;
    pointer_read_ = true;
    read_tick_ = now;
    if (!located)
        located = api_.cursor_position(&point) != FALSE;
    HWND foreign{};
    const auto seen =
        located ? read_window(point, foreign) : Pointer::unreadable;
    // Zero when unreadable: the next readable position counts as a move.
    point_ = point;
    ++reads_[static_cast<unsigned>(seen)];
    switch (seen) {
    case Pointer::game:
    case Pointer::cloaked:
    case Pointer::hidden:
        overlay_ = nullptr;
        break;
    case Pointer::foreign:
        // A button held down may be a drag the game captured the mouse for:
        // the overlay counts once it is let go.
        if (!buttons_down())
            overlay_ = foreign;
        break;
    case Pointer::unreadable:
        break;
    }
}

unsigned ForegroundCursor::restore_visibility(bool all) noexcept {
    // Undo our adjustments before the native activation handler makes its
    // own show/hide calls. No cursor position is restored or moved.
    unsigned hides = 0;
    if (all) {
        while (visibility_adjustments_) {
            observed_show(api_.show(FALSE));
            --visibility_adjustments_;
            ++hides;
        }
    } else if (visibility_adjustments_) {
        // Defense against an unbalanced module: the exact native depth is
        // not given back on return. With the recorded sequences the counter
        // stays at -1 in game, as before.
        observed_show(api_.show(FALSE));
        visibility_adjustments_ = 0;
        hides = 1;
    }
    visibility_limit_reached_ = false;
    return hides;
}

bool ForegroundCursor::detail() noexcept {
    if (!log_)
        return false;
    if (transition_lines_ < detailed_transitions) {
        ++transition_lines_;
        return true;
    }
    if (transition_lines_ == detailed_transitions) {
        ++transition_lines_;
        char reason[24]{};
        std::snprintf(reason, sizeof(reason), "limit=%u", detailed_transitions);
        log_("foreground-cursor-transitions-capped", reason);
    }
    return false;
}

void ForegroundCursor::release(Trigger trigger, HWND source) noexcept {
    if (released_)
        return;
    released_ = true;
    ++releases_;
    const char before = showing();
    const bool captured = api_.capture() == window_;
    if (captured)
        api_.release_capture();
    // Do not discard a rectangle another application installed meanwhile.
    RECT current{};
    const bool unclipped =
        clipped_ && api_.get_clip(&current) && equal(current, clip_);
    if (unclipped)
        api_.clip(nullptr);
    clipped_ = false;
    // Bounded even if the host already has a broken visibility counter.
    unsigned shows = 0;
    for (unsigned i = 0; i != visibility_cap; ++i) {
        const auto count = api_.show(TRUE);
        observed_show(count);
        ++visibility_adjustments_;
        ++shows;
        if (count >= 0)
            break;
        if (i == visibility_cap - 1)
            visibility_limit_reached_ = true;
    }
    if (arrow_)
        api_.set(arrow_);
    const char after = showing();
    if (!detail())
        return;
    // capture and clip: what this release took back from the game; showing:
    // the cursor state before and after it. The window the pointer went to
    // is named for a foreground loss or an overlay, not for a message.
    const char* event = visibility_limit_reached_
                            ? "foreground-cursor-visibility-cap"
                            : "foreground-cursor-released";
    char reason[224]{};
    if (trigger == Trigger::activation) {
        std::snprintf(reason, sizeof(reason),
                      "trigger=%s shows=%u count=%s capture=%d clip=%d "
                      "showing=%c/%c",
                      name(trigger), shows, counted(shows).text,
                      captured ? 1 : 0, unclipped ? 1 : 0, before, after);
    } else {
        const auto names = describe(source);
        std::snprintf(reason, sizeof(reason),
                      "trigger=%s shows=%u count=%s capture=%d clip=%d "
                      "showing=%c/%c image=%s class=%s",
                      name(trigger), shows, counted(shows).text,
                      captured ? 1 : 0, unclipped ? 1 : 0, before, after,
                      names.image, names.window_class);
    }
    log_(event, reason);
}

void ForegroundCursor::take_back(Trigger trigger) noexcept {
    const unsigned hides = restore_visibility(false);
    released_ = false;
    ++returns_;
    if (!detail())
        return;
    char reason[96]{};
    std::snprintf(reason, sizeof(reason),
                  "trigger=%s hides=%u count=%s blocked=%u", name(trigger),
                  hides, counted(hides).text, blocked_calls_);
    log_("foreground-cursor-returned", reason);
}

void ForegroundCursor::before_message(UINT message, WPARAM parameter) noexcept {
    if (!owner())
        return;
    if (inactive(message, parameter)) {
        deactivating_ = true;
        // The message path holds the pointer from now on; an overlay seen
        // before is read again after the return.
        overlay_ = nullptr;
    } else if (active(message, parameter)) {
        overlay_ = nullptr;
        if (released_)
            take_back(Trigger::activation);
        deactivating_ = false;
    } else if (message == WM_SETCURSOR && overlay_ != nullptr &&
               focused(api_.foreground())) {
        // The bound window has the pointer again: the overlay release ends
        // without waiting for a read. An overlay is held only by a release,
        // and never during a deactivation (cleared above, no read until the
        // return).
        overlay_ = nullptr;
        take_back(Trigger::overlay);
    }
}

void ForegroundCursor::after_message(UINT message, WPARAM parameter) noexcept {
    if (owner() && inactive(message, parameter))
        release(Trigger::activation, nullptr);
}

void ForegroundCursor::clipped(const RECT* rectangle, BOOL succeeded) noexcept {
    if (!owner() || !succeeded)
        return;
    clipped_ = rectangle != nullptr;
    if (rectangle)
        clip_ = *rectangle;
}

void ForegroundCursor::observed_show(int count) noexcept {
    if (owner()) {
        count_ = count;
        count_known_ = true;
    }
}

bool ForegroundCursor::skip_repeated_retail_hide(int& result) const noexcept {
    if (!owner() || !count_known_ || count_ >= 0)
        return false;
    result = count_;
    return true;
}

void ForegroundCursor::unbind(HWND window) noexcept {
    if (!owner() || window != window_)
        return;
    // A call the game makes during the teardown leaves the state alone.
    busy_ = true;
    if (event_hook_)
        api_.event_unhook(event_hook_);
    event_hook_ = nullptr;
    // Capture and clipping belong to the departing window; never move the
    // pointer or borrow capture from another thread/window during teardown.
    if (api_.capture() == window_)
        api_.release_capture();
    RECT current{};
    if (clipped_ && api_.get_clip(&current) && equal(current, clip_))
        api_.clip(nullptr);
    const unsigned hides = restore_visibility(true);
    // Logged while still bound: count_ is our last real result. Releases
    // and returns are counted beyond the lines written; reads by result.
    if (log_) {
        char reason[256]{};
        std::snprintf(reason, sizeof(reason),
                      "hides=%u count=%s blocked=%u releases=%u returns=%u "
                      "read_game=%u read_foreign=%u read_cloaked=%u "
                      "read_hidden=%u read_unreadable=%u",
                      hides, counted(hides).text, blocked_calls_, releases_,
                      returns_, reads_[0], reads_[1], reads_[2], reads_[3],
                      reads_[4]);
        log_("foreground-cursor-unbound", reason);
    }
    window_ = nullptr;
    thread_ = 0;
    arrow_ = nullptr;
    overlay_ = nullptr;
    point_ = {};
    process_ = 0;
    read_tick_ = 0;
    blocked_calls_ = releases_ = returns_ = 0;
    for (auto& reads : reads_)
        reads = 0;
    clipped_ = deactivating_ = released_ = false;
    foreground_lost_ = pointer_read_ = busy_ = false;
    count_known_ = false;
}

void SetActiveForegroundCursor(ForegroundCursor* cursor) noexcept {
    installed.store(cursor, std::memory_order_release);
}
} // namespace tdur::mod

void TdurBindForegroundCursor(HWND window) noexcept {
    if (auto* cursor = tdur::mod::installed.load(std::memory_order_acquire)) {
        DWORD process{};
        const auto thread = GetWindowThreadProcessId(window, &process);
        if (thread == GetCurrentThreadId() && process == GetCurrentProcessId())
            cursor->bind(window, thread, LoadCursorW(nullptr, IDC_ARROW));
    }
}

void TdurUnbindForegroundCursor(HWND window) noexcept {
    if (auto* cursor = tdur::mod::installed.load(std::memory_order_acquire))
        cursor->unbind(window);
}

void TdurForegroundCursorBefore(HWND window, UINT message,
                                WPARAM parameter) noexcept {
    if (auto* cursor = tdur::mod::installed.load(std::memory_order_acquire);
        cursor && cursor->bound_to(window))
        cursor->before_message(message, parameter);
}

void TdurForegroundCursorAfter(HWND window, UINT message,
                               WPARAM parameter) noexcept {
    if (auto* cursor = tdur::mod::installed.load(std::memory_order_acquire);
        cursor && cursor->bound_to(window))
        cursor->after_message(message, parameter);
}
