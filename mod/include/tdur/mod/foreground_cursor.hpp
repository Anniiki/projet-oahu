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
//
// The game owns the pointer while its HWND (or a child) has the Win32
// foreground and the window under the pointer belongs to its process. A
// foreground lost without activation messages, or a window of another
// process under the pointer (visible, not cloaked by DWM), releases the
// pointer as a deactivation does; ownership regained takes it back. Every
// intercepted call and every foreground WinEvent refresh ownership. The
// window under the pointer is read at most once per 50 ms, except on that
// event and, while an overlay holds the release, once the pointer has moved.
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
    DWORD(WINAPI* current_process)();
    ULONGLONG(WINAPI* tick)(); // monotonic milliseconds
    BOOL(WINAPI* cursor_position)(POINT*);
    HWND(WINAPI* window_from_point)(POINT);
    HWND(WINAPI* ancestor)(HWND, UINT);
    DWORD(WINAPI* window_process)(HWND, DWORD*);
    BOOL(WINAPI* visible)(HWND);
    HRESULT(WINAPI* window_attribute)(HWND, DWORD, void*, DWORD);
    HWINEVENTHOOK(WINAPI* event_hook)(DWORD, DWORD, HMODULE, WINEVENTPROC,
                                      DWORD, DWORD, DWORD);
    BOOL(WINAPI* event_unhook)(HWINEVENTHOOK);
    // Read for the log only (CURSOR_SHOWING), never the position.
    BOOL(WINAPI* cursor_info)(CURSORINFO*);
    // Mouse buttons held down, physical state.
    SHORT(WINAPI* key_state)(int);
    // For the log only: the image path of a process and a window class.
    // Both return the characters written, 0 on failure.
    DWORD(WINAPI* process_image)(DWORD, wchar_t*, DWORD);
    int(WINAPI* class_name)(HWND, wchar_t*, int);
};
// reason is null or a short "key=value" text: call and counter values, and
// the names of a few shell components, never a coordinate, rectangle or
// handle.
using ForegroundCursorLog = void (*)(const char* event,
                                     const char* reason) noexcept;

class ForegroundCursor {
  public:
    explicit ForegroundCursor(ForegroundCursorApi api,
                              ForegroundCursorLog log = nullptr) noexcept
        : api_(api), log_(log) {}
    void bind(HWND window, DWORD thread, HCURSOR arrow) noexcept;
    void unbind(HWND window) noexcept;
    // Owner thread: releases the pointer or takes it back on a change of
    // ownership only. Inert while a deactivation message holds the release.
    void poll() noexcept;
    // First thing of every intercepted call: poll(), then a refusable call
    // is refused, and counted, while the game does not own the pointer.
    // Returns true for a refused call.
    [[nodiscard]] bool intercept(bool refusable) noexcept;
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
    enum class Trigger : unsigned char { activation, foreground, overlay };
    // What a read found under the pointer; the order indexes reads_.
    enum class Pointer : unsigned char {
        game,
        foreign,
        cloaked,
        hidden,
        unreadable
    };
    struct Names {
        char image[32];
        char window_class[49];
    };
    struct CountText {
        char text[12];
    };
    // Installed at bind; Windows calls it on the owner thread, from its
    // message loop, for a foreground change in any process.
    static void CALLBACK foreground_event(HWINEVENTHOOK hook, DWORD event, HWND,
                                          LONG, LONG, DWORD, DWORD) noexcept;
    [[nodiscard]] static const char* name(Trigger trigger) noexcept;
    [[nodiscard]] bool owner() const noexcept;
    [[nodiscard]] bool focused(HWND foreground) const noexcept;
    void refresh(bool event) noexcept;
    void read(bool event) noexcept;
    [[nodiscard]] Pointer read_window(POINT point,
                                      HWND& foreign) const noexcept;
    [[nodiscard]] bool buttons_down() const noexcept;
    [[nodiscard]] char showing() const noexcept;
    [[nodiscard]] Names describe(HWND window) const noexcept;
    // True when this release or return is written in full: the first 64
    // are, later ones are only counted, with one line at the switch.
    [[nodiscard]] bool detail() noexcept;
    void release(Trigger trigger, HWND source) noexcept;
    void take_back(Trigger trigger) noexcept;
    // Returns the number of real hides made. all=false (return to the game):
    // one hide at most; all=true (unbind): every remaining compensation.
    unsigned restore_visibility(bool all) noexcept;
    // count_ after our own calls, "none" without calls.
    [[nodiscard]] CountText counted(unsigned calls) const noexcept;
    ForegroundCursorApi api_;
    ForegroundCursorLog log_{};
    std::atomic<HWND> window_{};
    std::atomic<DWORD> thread_{};
    HCURSOR arrow_{};
    RECT clip_{};
    HWINEVENTHOOK event_hook_{};
    // Root of the foreign window under the pointer at the last read.
    HWND overlay_{};
    // Pointer position at the last read; compared, never logged.
    POINT point_{};
    ULONGLONG read_tick_{};
    DWORD process_{};
    unsigned blocked_calls_{};
    unsigned releases_{};
    unsigned returns_{};
    unsigned reads_[5]{};
    // Release and return lines written over the policy's life.
    unsigned transition_lines_{};
    bool clipped_{};
    bool deactivating_{};
    bool released_{};
    // The last poll found the foreground outside the game.
    bool foreground_lost_{};
    bool pointer_read_{};
    // A release, return or teardown is running: a nested poll is ignored.
    bool busy_{};
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
