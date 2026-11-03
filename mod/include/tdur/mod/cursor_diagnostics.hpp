#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

// Opt-in, metadata-only cursor and capture diagnostics around activation
// changes. Records enabled only by TDUR_CURSOR_DIAGNOSTICS=1; otherwise
// the diagnostics marker returns at once. It records which public
// cursor operations run and how they return. It never records coordinates,
// rectangles, cursor or window handle values, input events, window text or
// addresses outside the main image (callers become main-image RVAs through the
// existing observation logger). It forces no cursor, focus or pause state.

// Reads TDUR_CURSOR_DIAGNOSTICS and, when it is exactly "1", attaches
// ShowCursor, SetCursor, ClipCursor and ReleaseCapture for diagnostics.
// Independently, TDUR_D3D12_DISPLAY=1 installs the foreground cursor policy,
// also intercepting SetCursorPos/SetCapture. With neither feature, no API is
// attached. Invalid diagnostics configuration or failed attachment returns
// false. Foreground behavior is documented in foreground_cursor.hpp.
bool TdurInstallCursorDiagnostics() noexcept;

// Called by the owned display subclass before DefSubclassProc. Only
// WM_ACTIVATEAPP and WM_ACTIVATE are classified; the message itself, its
// parameters and the native activation flags are never altered.
void TdurCursorActivationMarker(UINT message, WPARAM wparam) noexcept;

namespace tdur::mod {

enum class CursorDiagnosticsSetting : std::uint8_t {
    disabled,
    enabled,
    invalid
};

// value/length/last_error come from GetEnvironmentVariableW: length 0 with
// ERROR_ENVVAR_NOT_FOUND means absent. Only the exact text "1" enables.
[[nodiscard]] CursorDiagnosticsSetting
ParseCursorDiagnosticsSetting(const wchar_t* value, DWORD length,
                              DWORD last_error) noexcept;

// Real (or, in tests, injected) entry points. Each wrapper calls its entry
// exactly once with the caller's argument.
struct CursorApiTable {
    int(WINAPI* show_cursor)(BOOL) = nullptr;
    HCURSOR(WINAPI* set_cursor)(HCURSOR) = nullptr;
    BOOL(WINAPI* clip_cursor)(const RECT*) = nullptr;
    BOOL(WINAPI* release_capture)() = nullptr;
};

// Capacity of CursorRecord::reason. The longest reason is the period summary
// with its ten counters at UINT32_MAX: "period" and ten " name=4294967295"
// fields, 186 characters, plus the NUL. cursor_diagnostics_core.cpp derives
// that length from the table it prints and checks it with a static_assert;
// every operation or marker reason is under 40 characters.
inline constexpr std::size_t cursor_reason_capacity = 192;

// One bounded line. event and reason are classifications only; error is the
// thread last error after a failed ClipCursor/ReleaseCapture, else 0.
struct CursorRecord {
    const char* event = nullptr;
    char reason[cursor_reason_capacity]{};
    DWORD error = 0;
    const void* caller = nullptr;
};
using CursorSink = void (*)(const CursorRecord& record) noexcept;

struct CursorLimits {
    // Individual operation lines after start-up and after each marker.
    std::uint32_t lines_per_window = 16;
    // Activation markers written per process; later ones are only counted.
    std::uint32_t activation_lines = 256;
    // Hard cap for every line of this module, summaries included.
    std::uint32_t total_lines = 4096;
};

// Counts since the previous activation marker, written as one summary line.
struct CursorPeriod {
    std::uint32_t show_true{}, show_false{}, show_negative{};
    std::uint32_t set_null{}, set_handle{};
    std::uint32_t clip_rect{}, clip_null{}, clip_failed{};
    std::uint32_t release_ok{}, release_failed{};
};

// Writes "period show1=N show0=N hidden=N set0=N set1=N clip=N unclip=N
// clipfail=N release=N releasefail=N" into record.reason. All ten fields fit
// for any counter values (see cursor_reason_capacity).
void FormatCursorSummary(const CursorPeriod& period,
                         CursorRecord& record) noexcept;

class CursorDiagnostics {
  public:
    CursorDiagnostics(const CursorApiTable& api, CursorSink sink,
                      CursorLimits limits = {}) noexcept;
    CursorDiagnostics(const CursorDiagnostics&) = delete;
    CursorDiagnostics& operator=(const CursorDiagnostics&) = delete;

    // Each wrapper returns the entry's result unchanged and leaves the thread
    // last error exactly as the entry left it, after any record.
    int show_cursor(BOOL show, const void* caller) noexcept;
    HCURSOR set_cursor(HCURSOR cursor, const void* caller) noexcept;
    BOOL clip_cursor(const RECT* rect, const void* caller) noexcept;
    BOOL release_capture(const void* caller) noexcept;
    // Writes the summary of the elapsed period (if any operation ran), then
    // the marker, then reopens the per-window line budget. Other messages are
    // ignored without effect.
    void activation(UINT message, WPARAM wparam) noexcept;

    [[nodiscard]] std::uint32_t lines_written() const noexcept;
    [[nodiscard]] std::uint32_t markers_seen() const noexcept;

  private:
    [[nodiscard]] bool take_operation_line() noexcept;
    void write(CursorRecord& record) noexcept;

    CursorApiTable api_;
    CursorSink sink_;
    CursorLimits limits_;
    mutable SRWLOCK lock_ = SRWLOCK_INIT;
    std::uint32_t window_left_{};
    std::uint32_t markers_written_{};
    std::uint32_t markers_seen_{};
    std::uint32_t lines_{};
    CursorPeriod period_{};
};

// The instance reached by TdurCursorActivationMarker; null when disabled.
// Set by TdurInstallCursorDiagnostics (or by a test) before any marker.
void SetActiveCursorDiagnostics(CursorDiagnostics* diagnostics) noexcept;

} // namespace tdur::mod
