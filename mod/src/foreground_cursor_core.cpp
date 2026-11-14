#include "tdur/mod/foreground_cursor.hpp"

#include <cstdio>

namespace tdur::mod {
namespace {
std::atomic<ForegroundCursor*> installed{nullptr};
// Bound for the real shows of one focus loss, even with a broken counter.
constexpr unsigned visibility_cap = 4096;
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
} // namespace

bool ForegroundCursor::owner() const noexcept {
    return window_ && api_.current_thread() == thread_;
}

bool ForegroundCursor::background() const noexcept {
    if (!owner())
        return false;
    const auto foreground = api_.foreground();
    return deactivating_ ||
           (foreground != window_ && !api_.is_child(window_, foreground));
}

void ForegroundCursor::bind(HWND window, DWORD thread, HCURSOR arrow) noexcept {
    // Display registration and teardown run on the HWND thread. Additional
    // devices for the same HWND do not reset the visibility balance.
    if (window_ == window)
        return;
    if (window_ || !window || api_.current_thread() != thread)
        return;
    arrow_ = arrow;
    thread_ = thread;
    window_ = window;
    if (log_)
        log_("foreground-cursor-bound", nullptr);
}

void ForegroundCursor::log(const char* event, const char* key,
                           unsigned calls) const noexcept {
    if (!log_)
        return;
    // count_ is the value returned by our own last call when calls != 0.
    char reason[48]{};
    if (calls)
        std::snprintf(reason, sizeof(reason), "%s=%u count=%d", key, calls,
                      count_);
    else
        std::snprintf(reason, sizeof(reason), "%s=0 count=none", key);
    log_(event, reason);
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

void ForegroundCursor::release() noexcept {
    if (released_)
        return;
    released_ = true;
    if (api_.capture() == window_)
        api_.release_capture();
    // Do not discard a rectangle another application installed meanwhile.
    RECT current{};
    if (clipped_ && api_.get_clip(&current) && equal(current, clip_))
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
    log(visibility_limit_reached_ ? "foreground-cursor-visibility-cap"
                                  : "foreground-cursor-released",
        "shows", shows);
}

void ForegroundCursor::before_message(UINT message, WPARAM parameter) noexcept {
    if (!owner())
        return;
    if (inactive(message, parameter))
        deactivating_ = true;
    else if (active(message, parameter)) {
        const bool restoring = released_;
        const unsigned hides = restore_visibility(false);
        deactivating_ = false;
        released_ = false;
        if (restoring)
            log("foreground-cursor-returned", "hides", hides);
    }
}

void ForegroundCursor::after_message(UINT message, WPARAM parameter) noexcept {
    if (owner() && inactive(message, parameter))
        release();
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
    // Capture and clipping belong to the departing window; never move the
    // pointer or borrow capture from another thread/window during teardown.
    if (api_.capture() == window_)
        api_.release_capture();
    RECT current{};
    if (clipped_ && api_.get_clip(&current) && equal(current, clip_))
        api_.clip(nullptr);
    const unsigned hides = restore_visibility(true);
    // Logged while still bound: count_ is our last real result.
    log("foreground-cursor-unbound", "hides", hides);
    window_ = nullptr;
    thread_ = 0;
    arrow_ = nullptr;
    clipped_ = deactivating_ = released_ = false;
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
