#include "tdur/mod/cursor_diagnostics.hpp"

#include <atomic>
#include <cstdio>
#include <string>

namespace tdur::mod {
namespace {

std::atomic<CursorDiagnostics*> active{nullptr};

struct SummaryField {
    const char* name;
    std::uint32_t CursorPeriod::* value;
};

// One table for the printed summary and for its capacity check.
constexpr char summary_word[] = "period";
constexpr SummaryField summary_fields[] = {
    {"show1", &CursorPeriod::show_true},
    {"show0", &CursorPeriod::show_false},
    {"hidden", &CursorPeriod::show_negative},
    {"set0", &CursorPeriod::set_null},
    {"set1", &CursorPeriod::set_handle},
    {"clip", &CursorPeriod::clip_rect},
    {"unclip", &CursorPeriod::clip_null},
    {"clipfail", &CursorPeriod::clip_failed},
    {"release", &CursorPeriod::release_ok},
    {"releasefail", &CursorPeriod::release_failed},
};
constexpr std::size_t uint32_max_digits = 10; // 4294967295

// " name=" plus the widest uint32 for each field, after the leading word.
constexpr std::size_t LongestSummary() noexcept {
    std::size_t length = std::char_traits<char>::length(summary_word);
    for (const auto& field : summary_fields)
        length += 1 + std::char_traits<char>::length(field.name) + 1 +
                  uint32_max_digits;
    return length;
}
static_assert(LongestSummary() < cursor_reason_capacity,
              "a summary with every counter at UINT32_MAX must fit with "
              "its NUL");

[[nodiscard]] const char* ActivateState(WPARAM wparam) noexcept {
    switch (LOWORD(wparam)) {
    case WA_INACTIVE:
        return "inactive";
    case WA_ACTIVE:
        return "active";
    case WA_CLICKACTIVE:
        return "click";
    default:
        return "other";
    }
}

[[nodiscard]] bool PeriodEmpty(const CursorPeriod& period) noexcept {
    return period.show_true == 0 && period.show_false == 0 &&
           period.set_null == 0 && period.set_handle == 0 &&
           period.clip_rect == 0 && period.clip_null == 0 &&
           period.release_ok == 0 && period.release_failed == 0;
}

} // namespace

void FormatCursorSummary(const CursorPeriod& period,
                         CursorRecord& record) noexcept {
    constexpr std::size_t capacity = cursor_reason_capacity;
    int used = std::snprintf(record.reason, capacity, "%s", summary_word);
    for (const auto& field : summary_fields) {
        if (used < 0 || static_cast<std::size_t>(used) >= capacity)
            return;
        const int written = std::snprintf(
            record.reason + used, capacity - used, " %s=%u", field.name,
            static_cast<unsigned>(period.*field.value));
        if (written < 0)
            return;
        used += written;
    }
}

CursorDiagnosticsSetting
ParseCursorDiagnosticsSetting(const wchar_t* value, DWORD length,
                              DWORD last_error) noexcept {
    if (length == 0)
        return last_error == ERROR_ENVVAR_NOT_FOUND
                   ? CursorDiagnosticsSetting::disabled
                   : CursorDiagnosticsSetting::invalid;
    if (length == 1 && value != nullptr && value[0] == L'1')
        return CursorDiagnosticsSetting::enabled;
    return CursorDiagnosticsSetting::invalid;
}

CursorDiagnostics::CursorDiagnostics(const CursorApiTable& api, CursorSink sink,
                                     CursorLimits limits) noexcept
    : api_(api), sink_(sink), limits_(limits),
      window_left_(limits.lines_per_window) {}

bool CursorDiagnostics::take_operation_line() noexcept {
    if (window_left_ == 0 || lines_ >= limits_.total_lines)
        return false;
    --window_left_;
    return true;
}

void CursorDiagnostics::write(CursorRecord& record) noexcept {
    ++lines_;
    if (sink_ != nullptr)
        sink_(record);
}

int CursorDiagnostics::show_cursor(BOOL show, const void* caller) noexcept {
    const int count = api_.show_cursor(show);
    const DWORD error = GetLastError();
    AcquireSRWLockExclusive(&lock_);
    ++(show ? period_.show_true : period_.show_false);
    if (count < 0)
        ++period_.show_negative;
    if (take_operation_line()) {
        CursorRecord record{"cursor-op"};
        std::snprintf(record.reason, sizeof(record.reason),
                      "show-cursor arg=%s count=%d", show ? "true" : "false",
                      count);
        record.caller = caller;
        write(record);
    }
    ReleaseSRWLockExclusive(&lock_);
    SetLastError(error);
    return count;
}

HCURSOR CursorDiagnostics::set_cursor(HCURSOR cursor,
                                      const void* caller) noexcept {
    const HCURSOR previous = api_.set_cursor(cursor);
    const DWORD error = GetLastError();
    AcquireSRWLockExclusive(&lock_);
    ++(cursor ? period_.set_handle : period_.set_null);
    if (take_operation_line()) {
        CursorRecord record{"cursor-op"};
        std::snprintf(record.reason, sizeof(record.reason),
                      "set-cursor arg=%s previous=%s",
                      cursor ? "handle" : "null", previous ? "handle" : "null");
        record.caller = caller;
        write(record);
    }
    ReleaseSRWLockExclusive(&lock_);
    SetLastError(error);
    return previous;
}

BOOL CursorDiagnostics::clip_cursor(const RECT* rect,
                                    const void* caller) noexcept {
    const BOOL result = api_.clip_cursor(rect);
    const DWORD error = GetLastError();
    AcquireSRWLockExclusive(&lock_);
    ++(rect ? period_.clip_rect : period_.clip_null);
    if (!result)
        ++period_.clip_failed;
    if (take_operation_line()) {
        CursorRecord record{"cursor-op"};
        std::snprintf(record.reason, sizeof(record.reason),
                      "clip-cursor arg=%s result=%d", rect ? "rect" : "null",
                      result ? 1 : 0);
        record.error = result ? 0 : error;
        record.caller = caller;
        write(record);
    }
    ReleaseSRWLockExclusive(&lock_);
    SetLastError(error);
    return result;
}

BOOL CursorDiagnostics::release_capture(const void* caller) noexcept {
    const BOOL result = api_.release_capture();
    const DWORD error = GetLastError();
    AcquireSRWLockExclusive(&lock_);
    ++(result ? period_.release_ok : period_.release_failed);
    if (take_operation_line()) {
        CursorRecord record{"cursor-op"};
        std::snprintf(record.reason, sizeof(record.reason),
                      "release-capture result=%d", result ? 1 : 0);
        record.error = result ? 0 : error;
        record.caller = caller;
        write(record);
    }
    ReleaseSRWLockExclusive(&lock_);
    SetLastError(error);
    return result;
}

void CursorDiagnostics::activation(UINT message, WPARAM wparam) noexcept {
    if (message != WM_ACTIVATEAPP && message != WM_ACTIVATE)
        return;
    const DWORD error = GetLastError();
    AcquireSRWLockExclusive(&lock_);
    ++markers_seen_;
    if (!PeriodEmpty(period_) && lines_ < limits_.total_lines) {
        CursorRecord summary{"cursor-summary"};
        FormatCursorSummary(period_, summary);
        write(summary);
    }
    period_ = {};
    if (markers_written_ < limits_.activation_lines &&
        lines_ < limits_.total_lines) {
        CursorRecord marker{"cursor-activation"};
        if (message == WM_ACTIVATEAPP)
            std::snprintf(marker.reason, sizeof(marker.reason),
                          "activateapp active=%d", wparam ? 1 : 0);
        else
            std::snprintf(marker.reason, sizeof(marker.reason),
                          "activate state=%s minimized=%d",
                          ActivateState(wparam), HIWORD(wparam) ? 1 : 0);
        ++markers_written_;
        write(marker);
    }
    window_left_ = limits_.lines_per_window;
    ReleaseSRWLockExclusive(&lock_);
    SetLastError(error);
}

std::uint32_t CursorDiagnostics::lines_written() const noexcept {
    AcquireSRWLockShared(&lock_);
    const auto value = lines_;
    ReleaseSRWLockShared(&lock_);
    return value;
}

std::uint32_t CursorDiagnostics::markers_seen() const noexcept {
    AcquireSRWLockShared(&lock_);
    const auto value = markers_seen_;
    ReleaseSRWLockShared(&lock_);
    return value;
}

void SetActiveCursorDiagnostics(CursorDiagnostics* diagnostics) noexcept {
    active.store(diagnostics, std::memory_order_release);
}

} // namespace tdur::mod

void TdurCursorActivationMarker(UINT message, WPARAM wparam) noexcept {
    if (auto* diagnostics = tdur::mod::active.load(std::memory_order_acquire))
        diagnostics->activation(message, wparam);
}
