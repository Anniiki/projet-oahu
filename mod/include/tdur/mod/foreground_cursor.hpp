#pragma once

#include <windows.h>

#include <atomic>

namespace tdur::mod {

// An application policy, not a reconstruction of a commercial function.
// The owner HWND thread alone owns this state. Tests inject every desktop API.
// On focus loss the cursor is made visible with at most 4,096 real shows.
// On return a single hide is made, not the whole loss compensation: a module
// that hides without showing again cannot deepen the counter cycle after
// cycle. At unbind every remaining compensation is still given back.
struct ForegroundCursorApi {
    DWORD(WINAPI* current_thread)();
    HWND(WINAPI* foreground)();
    BOOL(WINAPI* is_child)(HWND, HWND);
    HWND(WINAPI* capture)();
    BOOL(WINAPI* release_capture)();
    BOOL(WINAPI* clip)(const RECT*);
    BOOL(WINAPI* get_clip)(RECT*);
    int(WINAPI* show)(BOOL);
    HCURSOR(WINAPI* set)(HCURSOR);
};
// reason is null or a short "key=value" text: call and counter values only,
// never a coordinate, rectangle or handle.
using ForegroundCursorLog = void (*)(const char* event,
                                     const char* reason) noexcept;

class ForegroundCursor {
  public:
    explicit ForegroundCursor(ForegroundCursorApi api,
                              ForegroundCursorLog log = nullptr) noexcept
        : api_(api), log_(log) {}
    void bind(HWND window, DWORD thread, HCURSOR arrow) noexcept;
    void unbind(HWND window) noexcept;
    [[nodiscard]] bool background() const noexcept;
    [[nodiscard]] bool bound_to(HWND window) const noexcept {
        return window_ == window;
    }
    // Called around the native procedure; messages themselves are unchanged.
    void before_message(UINT message, WPARAM parameter) noexcept;
    void after_message(UINT message, WPARAM parameter) noexcept;
    void clipped(const RECT* rectangle, BOOL succeeded) noexcept;
    void observed_show(int count) noexcept;
    [[nodiscard]] bool skip_repeated_retail_hide(int& result) const noexcept;

    // Background ShowCursor calls have no ownership of the desktop cursor.
    // Return terminal values for conventional show/hide loops, without
    // accumulating a hidden-counter debt while the game runs unfocused.
    [[nodiscard]] static int background_show_result(BOOL show) noexcept {
        return show ? 0 : -1;
    }
    [[nodiscard]] unsigned visibility_adjustments() const noexcept {
        return visibility_adjustments_;
    }
    [[nodiscard]] bool visibility_limit_reached() const noexcept {
        return visibility_limit_reached_;
    }

  private:
    [[nodiscard]] bool owner() const noexcept;
    void release() noexcept;
    // Returns the number of real hides made. all=false (return to the game):
    // one hide at most; all=true (unbind): every remaining compensation.
    unsigned restore_visibility(bool all) noexcept;
    // "<key>=<calls> count=<last API result>", or count=none without calls.
    void log(const char* event, const char* key, unsigned calls) const noexcept;
    ForegroundCursorApi api_;
    ForegroundCursorLog log_{};
    std::atomic<HWND> window_{};
    std::atomic<DWORD> thread_{};
    HCURSOR arrow_{};
    RECT clip_{};
    bool clipped_{};
    bool deactivating_{};
    bool released_{};
    bool visibility_limit_reached_{};
    bool count_known_{};
    int count_{};
    unsigned visibility_adjustments_{};
};

void SetActiveForegroundCursor(ForegroundCursor* cursor) noexcept;

} // namespace tdur::mod

// Process-lifetime optional policy attached by the existing cursor installer.
// The display owner calls these; they never activate a window or move a mouse.
void TdurBindForegroundCursor(HWND window) noexcept;
void TdurUnbindForegroundCursor(HWND window) noexcept;
void TdurForegroundCursorBefore(HWND window, UINT message,
                                WPARAM parameter) noexcept;
void TdurForegroundCursorAfter(HWND window, UINT message,
                               WPARAM parameter) noexcept;
