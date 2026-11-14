#pragma once

#include "tdur/mod/cursor_diagnostics.hpp"

#include <d3d9.h>
#include <windows.h>

#include <cstdint>

// Opt-in, metadata-only diagnostics of the operations made on the bridged
// game window and of the presentation parameters the game requests at each
// CreateDevice/Reset (C3), with the origin class of each caller (C5).
// Enabled with the cursor diagnostics (TDUR_CURSOR_DIAGNOSTICS=1); without
// an active instance every entry point returns at once. It records sizes,
// flags, commands and style classes only: never a coordinate, a window or
// insert-after handle value, window text or module name. It never alters a
// message, a parameter or a value returned to the game.

namespace tdur::mod {

enum class WindowOp : std::uint8_t {
    set_pos,
    move,
    set_style,
    set_exstyle,
    placement,
    show,
    show_async
};
// "setpos", "move", "style", "exstyle", "placement", "show", "show-async".
[[nodiscard]] const char* WindowOpName(WindowOp op) noexcept;

struct WindowCall {
    WindowOp op{};
    int width{}, height{};      // requested size, no coordinates
    UINT flags{};               // SWP_* for set_pos
    int command{};              // SW_* for show/placement
    LONG_PTR style{};           // new style for set_style/set_exstyle
    bool fits_monitor{};        // requested rect equals a monitor's bounds
    const char* order = "none"; // none|top|bottom|topmost|notopmost|window
};

// Values the game asked for, before the bridge rewrites them.
struct PresentRequest {
    unsigned windowed{}, width{}, height{}, format{}, count{}, msaa{}, swap{},
        flags{}, refresh{}, interval{};
};
[[nodiscard]] PresentRequest
DescribePresentRequest(const D3DPRESENT_PARAMETERS& parameters) noexcept;

struct WindowLimits {
    // Individual window-op/window-msg lines per activation period.
    std::uint32_t lines_per_window = 32;
    // display-present-request lines per process.
    std::uint32_t present_requests = 64;
    // Hard cap for every line of this module, summaries included.
    std::uint32_t total_lines = 2048;
};

struct MessageContext {
    bool internal{};    // DisplayWindowOperationActive()
    bool constrained{}; // C2 changed the parameters of this message
    long requested_width{}, requested_height{}; // before C2, if constrained
    bool minimized{};                           // IsIconic() after the message
};

// Counts since the previous activation marker, written as window-summary.
struct WindowPeriod {
    std::uint32_t ops{}, proxy{}, main{}, ext{}, setpos{}, show{}, style{},
        placement{}, move{}, changing{}, changed{}, size{}, syscmd{},
        constrained{};
};
// "ops=N proxy=N main=N ext=N setpos=N show=N style=N placement=N move=N
// changing=N changed=N size=N syscmd=N constrained=N"; every field fits.
void FormatWindowSummary(const WindowPeriod& period,
                         CursorRecord& record) noexcept;

// The operation intercepted on this thread, for "during=" attribution.
struct WindowCallState {
    WindowOp op{};
    CallerOrigin origin{};
    bool active{};
};
[[nodiscard]] WindowCallState CurrentWindowCall() noexcept;

// RAII on the calling thread: the API in progress, restored on exit, so
// nested calls (a hook calling another hooked API) are attributed correctly.
class WindowCallScope {
  public:
    WindowCallScope(WindowOp op, CallerOrigin origin) noexcept;
    ~WindowCallScope();
    WindowCallScope(const WindowCallScope&) = delete;
    WindowCallScope& operator=(const WindowCallScope&) = delete;

  private:
    WindowCallState previous_;
};

class WindowDiagnostics {
  public:
    explicit WindowDiagnostics(CursorSink sink,
                               WindowLimits limits = {}) noexcept;
    WindowDiagnostics(const WindowDiagnostics&) = delete;
    WindowDiagnostics& operator=(const WindowDiagnostics&) = delete;

    // One bound HWND: the bridged game window. Other HWNDs are ignored.
    void bind(HWND window) noexcept;
    void unbind(HWND window) noexcept;
    [[nodiscard]] bool bound_to(HWND window) const noexcept;
    // Whether an individual line can still be written in this period; lets
    // a hook skip optional monitor queries when the line would be dropped.
    [[nodiscard]] bool line_available() const noexcept;

    // Written before the real call, so the messages it causes follow it.
    void api_call(HWND window, const WindowCall& call, CallerOrigin origin,
                  const void* caller) noexcept;
    void api_failed(HWND window, WindowOp op, DWORD error, CallerOrigin origin,
                    const void* caller) noexcept;
    // After the native handler (and C2): the parameters as Windows applies
    // them. Messages other than the documented ones are ignored.
    void message(HWND window, UINT message, WPARAM wparam, LPARAM lparam,
                 const MessageContext& context) noexcept;
    // Writes the summary of the elapsed period, if any, and reopens the
    // per-period line budget. Other messages are ignored.
    void activation(UINT message, WPARAM wparam) noexcept;
    void present_request(const char* call, const PresentRequest& request,
                         const void* caller) noexcept;

    [[nodiscard]] std::uint32_t lines_written() const noexcept;

  private:
    [[nodiscard]] bool take_line() noexcept;
    void write(CursorRecord& record) noexcept;

    CursorSink sink_;
    WindowLimits limits_;
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    HWND window_{};
    std::uint32_t window_left_{};
    std::uint32_t present_left_{};
    std::uint32_t lines_{};
    WindowPeriod period_{};
};

// The instance reached by the Tdur* entry points below; null when disabled.
void SetActiveWindowDiagnostics(WindowDiagnostics* diagnostics) noexcept;
[[nodiscard]] WindowDiagnostics* ActiveWindowDiagnostics() noexcept;

} // namespace tdur::mod

// Called by the display bridge; no effect without an active instance.
void TdurWindowDiagnosticsBind(HWND window) noexcept;
void TdurWindowDiagnosticsUnbind(HWND window) noexcept;
[[nodiscard]] bool TdurWindowDiagnosticsBound(HWND window) noexcept;
void TdurWindowActivationMarker(UINT message, WPARAM wparam) noexcept;
void TdurWindowDiagnosticsMessage(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    const tdur::mod::MessageContext& context) noexcept;
void TdurWindowDiagnosticsPresentRequest(
    const char* call, const D3DPRESENT_PARAMETERS& parameters,
    const void* caller) noexcept;
