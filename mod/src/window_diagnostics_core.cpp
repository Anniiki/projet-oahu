#include "tdur/mod/window_diagnostics.hpp"

#include <atomic>
#include <cstdio>
#include <iterator>
#include <string>

namespace tdur::mod {
namespace {

std::atomic<WindowDiagnostics*> active{nullptr};
thread_local WindowCallState current{};

struct SummaryField {
    const char* name;
    std::uint32_t WindowPeriod::* value;
};
constexpr SummaryField summary_fields[] = {
    {"ops", &WindowPeriod::ops},
    {"proxy", &WindowPeriod::proxy},
    {"main", &WindowPeriod::main},
    {"ext", &WindowPeriod::ext},
    {"setpos", &WindowPeriod::setpos},
    {"show", &WindowPeriod::show},
    {"style", &WindowPeriod::style},
    {"placement", &WindowPeriod::placement},
    {"move", &WindowPeriod::move},
    {"changing", &WindowPeriod::changing},
    {"changed", &WindowPeriod::changed},
    {"size", &WindowPeriod::size},
    {"syscmd", &WindowPeriod::syscmd},
    {"constrained", &WindowPeriod::constrained},
};
constexpr std::size_t uint32_max_digits = 10; // 4294967295

// "name=4294967295" per field, separated by one space.
constexpr std::size_t LongestSummary() noexcept {
    std::size_t length = 0;
    for (const auto& field : summary_fields)
        length += std::char_traits<char>::length(field.name) + 1 +
                  uint32_max_digits + 1;
    return length - 1;
}
static_assert(LongestSummary() < cursor_reason_capacity,
              "a window summary with every counter at UINT32_MAX must fit "
              "with its NUL");

constexpr const char* op_names[] = {"setpos",    "move", "style",     "exstyle",
                                    "placement", "show", "show-async"};
static_assert(std::size(op_names) ==
                  static_cast<std::size_t>(WindowOp::show_async) + 1,
              "one name per window operation");

[[nodiscard]] unsigned Bit(LONG_PTR value, LONG_PTR mask) noexcept {
    return (value & mask) == mask ? 1U : 0U;
}

constexpr LONG_PTR edge_styles = WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE |
                                 WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;

[[nodiscard]] const char* SysCommandName(WPARAM wparam) noexcept {
    switch (wparam & 0xFFF0) {
    case SC_MINIMIZE:
        return "minimize";
    case SC_RESTORE:
        return "restore";
    case SC_MAXIMIZE:
        return "maximize";
    case SC_CLOSE:
        return "close";
    case SC_KEYMENU:
        return "keymenu";
    default:
        return "other";
    }
}

[[nodiscard]] bool PeriodEmpty(const WindowPeriod& period) noexcept {
    for (const auto& field : summary_fields)
        if (period.*field.value != 0)
            return false;
    return true;
}

// Size, position or visibility changes only; z-order-only traffic is
// counted nowhere and written nowhere.
[[nodiscard]] bool Geometric(UINT flags) noexcept {
    return (flags & SWP_NOSIZE) == 0 || (flags & SWP_NOMOVE) == 0 ||
           (flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW)) != 0;
}

void During(char (&text)[32]) noexcept {
    const auto state = current;
    if (state.active)
        std::snprintf(text, sizeof(text), "%s/%s", WindowOpName(state.op),
                      CallerOriginName(state.origin));
    else
        std::snprintf(text, sizeof(text), "none/none");
}

} // namespace

const char* WindowOpName(WindowOp op) noexcept {
    const auto index = static_cast<std::size_t>(op);
    return index < std::size(op_names) ? op_names[index] : "other";
}

PresentRequest
DescribePresentRequest(const D3DPRESENT_PARAMETERS& parameters) noexcept {
    PresentRequest request;
    request.windowed = parameters.Windowed ? 1U : 0U;
    request.width = parameters.BackBufferWidth;
    request.height = parameters.BackBufferHeight;
    request.format = static_cast<unsigned>(parameters.BackBufferFormat);
    request.count = parameters.BackBufferCount;
    request.msaa = static_cast<unsigned>(parameters.MultiSampleType);
    request.swap = static_cast<unsigned>(parameters.SwapEffect);
    request.flags = parameters.Flags;
    request.refresh = parameters.FullScreen_RefreshRateInHz;
    request.interval = parameters.PresentationInterval;
    return request;
}

void FormatWindowSummary(const WindowPeriod& period,
                         CursorRecord& record) noexcept {
    constexpr std::size_t capacity = cursor_reason_capacity;
    int used = 0;
    record.reason[0] = '\0';
    for (const auto& field : summary_fields) {
        if (used < 0 || static_cast<std::size_t>(used) >= capacity)
            return;
        const int written =
            std::snprintf(record.reason + used, capacity - used, "%s%s=%u",
                          used == 0 ? "" : " ", field.name,
                          static_cast<unsigned>(period.*field.value));
        if (written < 0)
            return;
        used += written;
    }
}

WindowCallState CurrentWindowCall() noexcept { return current; }

WindowCallScope::WindowCallScope(WindowOp op, CallerOrigin origin) noexcept
    : previous_(current) {
    current = {op, origin, true};
}

WindowCallScope::~WindowCallScope() { current = previous_; }

WindowDiagnostics::WindowDiagnostics(CursorSink sink,
                                     WindowLimits limits) noexcept
    : sink_(sink), limits_(limits), window_left_(limits.lines_per_window),
      present_left_(limits.present_requests) {}

bool WindowDiagnostics::take_line() noexcept {
    if (window_left_ == 0 || lines_ >= limits_.total_lines)
        return false;
    --window_left_;
    return true;
}

void WindowDiagnostics::write(CursorRecord& record) noexcept {
    ++lines_;
    if (sink_ != nullptr)
        sink_(record);
}

void WindowDiagnostics::bind(HWND window) noexcept {
    if (window == nullptr)
        return;
    const DWORD error = GetLastError();
    AcquireSRWLockExclusive(&lock_);
    if (window_ != window) {
        window_ = window;
        if (lines_ < limits_.total_lines) {
            CursorRecord record{"window-diag-bound"};
            write(record);
        }
    }
    ReleaseSRWLockExclusive(&lock_);
    SetLastError(error);
}

void WindowDiagnostics::unbind(HWND window) noexcept {
    const DWORD error = GetLastError();
    AcquireSRWLockExclusive(&lock_);
    if (window != nullptr && window_ == window) {
        window_ = nullptr;
        if (lines_ < limits_.total_lines) {
            CursorRecord record{"window-diag-unbound"};
            write(record);
        }
    }
    ReleaseSRWLockExclusive(&lock_);
    SetLastError(error);
}

bool WindowDiagnostics::bound_to(HWND window) const noexcept {
    AcquireSRWLockShared(&lock_);
    const bool bound = window != nullptr && window_ == window;
    ReleaseSRWLockShared(&lock_);
    return bound;
}

bool WindowDiagnostics::line_available() const noexcept {
    AcquireSRWLockShared(&lock_);
    const bool available = window_left_ != 0 && lines_ < limits_.total_lines;
    ReleaseSRWLockShared(&lock_);
    return available;
}

void WindowDiagnostics::api_call(HWND window, const WindowCall& call,
                                 CallerOrigin origin,
                                 const void* caller) noexcept {
    const DWORD error = GetLastError();
    AcquireSRWLockExclusive(&lock_);
    if (window == nullptr || window != window_) {
        ReleaseSRWLockExclusive(&lock_);
        SetLastError(error);
        return;
    }
    ++period_.ops;
    if (origin == CallerOrigin::proxy)
        ++period_.proxy;
    else if (origin == CallerOrigin::main ||
             origin == CallerOrigin::retail_site)
        ++period_.main;
    else
        ++period_.ext;
    switch (call.op) {
    case WindowOp::set_pos:
        ++period_.setpos;
        break;
    case WindowOp::move:
        ++period_.move;
        break;
    case WindowOp::set_style:
    case WindowOp::set_exstyle:
        ++period_.style;
        break;
    case WindowOp::placement:
        ++period_.placement;
        break;
    case WindowOp::show:
    case WindowOp::show_async:
        ++period_.show;
        break;
    }
    if (take_line()) {
        CursorRecord record{"window-op"};
        const char* from = CallerOriginName(origin);
        switch (call.op) {
        case WindowOp::set_pos:
            std::snprintf(record.reason, sizeof(record.reason),
                          "op=setpos w=%d h=%d flags=0x%04x size=%u move=%u "
                          "order=%s fits=%u origin=%s",
                          call.width, call.height, call.flags,
                          (call.flags & SWP_NOSIZE) ? 0U : 1U,
                          (call.flags & SWP_NOMOVE) ? 0U : 1U,
                          call.order ? call.order : "none",
                          call.fits_monitor ? 1U : 0U, from);
            break;
        case WindowOp::move:
            std::snprintf(record.reason, sizeof(record.reason),
                          "op=move w=%d h=%d fits=%u origin=%s", call.width,
                          call.height, call.fits_monitor ? 1U : 0U, from);
            break;
        case WindowOp::set_style:
            std::snprintf(
                record.reason, sizeof(record.reason),
                "op=style caption=%u frame=%u popup=%u minimize=%u "
                "maximize=%u visible=%u origin=%s",
                Bit(call.style, WS_CAPTION), Bit(call.style, WS_THICKFRAME),
                Bit(call.style, WS_POPUP), Bit(call.style, WS_MINIMIZE),
                Bit(call.style, WS_MAXIMIZE), Bit(call.style, WS_VISIBLE),
                from);
            break;
        case WindowOp::set_exstyle:
            std::snprintf(record.reason, sizeof(record.reason),
                          "op=exstyle edges=%u topmost=%u origin=%s",
                          (call.style & edge_styles) ? 1U : 0U,
                          Bit(call.style, WS_EX_TOPMOST), from);
            break;
        case WindowOp::placement:
            std::snprintf(record.reason, sizeof(record.reason),
                          "op=placement show=%d w=%d h=%d origin=%s",
                          call.command, call.width, call.height, from);
            break;
        case WindowOp::show:
        case WindowOp::show_async:
            std::snprintf(record.reason, sizeof(record.reason),
                          "op=%s cmd=%d origin=%s", WindowOpName(call.op),
                          call.command, from);
            break;
        }
        record.caller = caller;
        write(record);
    }
    ReleaseSRWLockExclusive(&lock_);
    SetLastError(error);
}

void WindowDiagnostics::api_failed(HWND window, WindowOp op, DWORD error,
                                   CallerOrigin origin,
                                   const void* caller) noexcept {
    const DWORD saved = GetLastError();
    AcquireSRWLockExclusive(&lock_);
    if (window != nullptr && window == window_ && take_line()) {
        CursorRecord record{"window-op-failed"};
        std::snprintf(record.reason, sizeof(record.reason), "op=%s origin=%s",
                      WindowOpName(op), CallerOriginName(origin));
        record.error = error;
        record.caller = caller;
        write(record);
    }
    ReleaseSRWLockExclusive(&lock_);
    SetLastError(saved);
}

void WindowDiagnostics::message(HWND window, UINT message, WPARAM wparam,
                                LPARAM lparam,
                                const MessageContext& context) noexcept {
    switch (message) {
    case WM_WINDOWPOSCHANGING:
    case WM_WINDOWPOSCHANGED:
    case WM_SIZE:
    case WM_STYLECHANGED:
    case WM_SYSCOMMAND:
    case WM_SHOWWINDOW:
    case WM_DISPLAYCHANGE:
        break;
    default:
        return;
    }
    const DWORD error = GetLastError();
    char during[32]{};
    During(during);
    AcquireSRWLockExclusive(&lock_);
    if (window == nullptr || window != window_) {
        ReleaseSRWLockExclusive(&lock_);
        SetLastError(error);
        return;
    }
    CursorRecord record{"window-msg"};
    bool wanted = true;
    const unsigned internal = context.internal ? 1U : 0U;
    switch (message) {
    case WM_WINDOWPOSCHANGING:
    case WM_WINDOWPOSCHANGED: {
        const auto* position = reinterpret_cast<const WINDOWPOS*>(lparam);
        if (position == nullptr || !Geometric(position->flags)) {
            wanted = false;
            break;
        }
        if (message == WM_WINDOWPOSCHANGING) {
            ++period_.changing;
            if (context.constrained)
                ++period_.constrained;
            const long requestedWidth =
                context.constrained ? context.requested_width : position->cx;
            const long requestedHeight =
                context.constrained ? context.requested_height : position->cy;
            std::snprintf(record.reason, sizeof(record.reason),
                          "msg=poschanging req=%ldx%ld out=%dx%d flags=0x%04x "
                          "constrained=%u internal=%u during=%s",
                          requestedWidth, requestedHeight, position->cx,
                          position->cy, position->flags,
                          context.constrained ? 1U : 0U, internal, during);
        } else {
            ++period_.changed;
            std::snprintf(record.reason, sizeof(record.reason),
                          "msg=poschanged w=%d h=%d flags=0x%04x minimized=%u "
                          "internal=%u during=%s",
                          position->cx, position->cy, position->flags,
                          context.minimized ? 1U : 0U, internal, during);
        }
        break;
    }
    case WM_SIZE:
        ++period_.size;
        std::snprintf(record.reason, sizeof(record.reason),
                      "msg=size type=%u w=%u h=%u internal=%u during=%s",
                      static_cast<unsigned>(wparam),
                      static_cast<unsigned>(LOWORD(lparam)),
                      static_cast<unsigned>(HIWORD(lparam)), internal, during);
        break;
    case WM_STYLECHANGED: {
        const auto* change = reinterpret_cast<const STYLESTRUCT*>(lparam);
        if (change == nullptr) {
            wanted = false;
            break;
        }
        const auto value = static_cast<LONG_PTR>(change->styleNew);
        if (static_cast<int>(wparam) == GWL_EXSTYLE)
            std::snprintf(record.reason, sizeof(record.reason),
                          "msg=exstylechanged edges=%u topmost=%u internal=%u",
                          (value & edge_styles) ? 1U : 0U,
                          Bit(value, WS_EX_TOPMOST), internal);
        else
            std::snprintf(record.reason, sizeof(record.reason),
                          "msg=stylechanged caption=%u frame=%u popup=%u "
                          "minimize=%u maximize=%u internal=%u",
                          Bit(value, WS_CAPTION), Bit(value, WS_THICKFRAME),
                          Bit(value, WS_POPUP), Bit(value, WS_MINIMIZE),
                          Bit(value, WS_MAXIMIZE), internal);
        break;
    }
    case WM_SYSCOMMAND:
        ++period_.syscmd;
        std::snprintf(record.reason, sizeof(record.reason),
                      "msg=syscommand cmd=%s internal=%u during=%s",
                      SysCommandName(wparam), internal, during);
        break;
    case WM_SHOWWINDOW:
        std::snprintf(record.reason, sizeof(record.reason),
                      "msg=showwindow show=%u status=%u", wparam ? 1U : 0U,
                      static_cast<unsigned>(lparam));
        break;
    case WM_DISPLAYCHANGE:
        std::snprintf(record.reason, sizeof(record.reason),
                      "msg=displaychange w=%u h=%u bpp=%u",
                      static_cast<unsigned>(LOWORD(lparam)),
                      static_cast<unsigned>(HIWORD(lparam)),
                      static_cast<unsigned>(wparam));
        break;
    default:
        wanted = false;
        break;
    }
    if (wanted && take_line())
        write(record);
    ReleaseSRWLockExclusive(&lock_);
    SetLastError(error);
}

void WindowDiagnostics::activation(UINT message, WPARAM) noexcept {
    if (message != WM_ACTIVATEAPP && message != WM_ACTIVATE)
        return;
    const DWORD error = GetLastError();
    AcquireSRWLockExclusive(&lock_);
    if (!PeriodEmpty(period_) && lines_ < limits_.total_lines) {
        CursorRecord summary{"window-summary"};
        FormatWindowSummary(period_, summary);
        write(summary);
    }
    period_ = {};
    window_left_ = limits_.lines_per_window;
    ReleaseSRWLockExclusive(&lock_);
    SetLastError(error);
}

void WindowDiagnostics::present_request(const char* call,
                                        const PresentRequest& request,
                                        const void* caller) noexcept {
    const DWORD error = GetLastError();
    AcquireSRWLockExclusive(&lock_);
    if (present_left_ != 0 && lines_ < limits_.total_lines) {
        --present_left_;
        CursorRecord record{"display-present-request"};
        std::snprintf(record.reason, sizeof(record.reason),
                      "call=%s windowed=%u bb=%ux%u fmt=%u count=%u msaa=%u "
                      "swap=%u flags=0x%x refresh=%u interval=0x%x",
                      call ? call : "unknown", request.windowed, request.width,
                      request.height, request.format, request.count,
                      request.msaa, request.swap, request.flags,
                      request.refresh, request.interval);
        record.caller = caller;
        write(record);
    }
    ReleaseSRWLockExclusive(&lock_);
    SetLastError(error);
}

std::uint32_t WindowDiagnostics::lines_written() const noexcept {
    AcquireSRWLockShared(&lock_);
    const auto value = lines_;
    ReleaseSRWLockShared(&lock_);
    return value;
}

void SetActiveWindowDiagnostics(WindowDiagnostics* diagnostics) noexcept {
    active.store(diagnostics, std::memory_order_release);
}

WindowDiagnostics* ActiveWindowDiagnostics() noexcept {
    return active.load(std::memory_order_acquire);
}

} // namespace tdur::mod

void TdurWindowDiagnosticsBind(HWND window) noexcept {
    if (auto* diagnostics = tdur::mod::ActiveWindowDiagnostics())
        diagnostics->bind(window);
}

void TdurWindowDiagnosticsUnbind(HWND window) noexcept {
    if (auto* diagnostics = tdur::mod::ActiveWindowDiagnostics())
        diagnostics->unbind(window);
}

bool TdurWindowDiagnosticsBound(HWND window) noexcept {
    auto* diagnostics = tdur::mod::ActiveWindowDiagnostics();
    return diagnostics != nullptr && diagnostics->bound_to(window);
}

void TdurWindowActivationMarker(UINT message, WPARAM wparam) noexcept {
    if (auto* diagnostics = tdur::mod::ActiveWindowDiagnostics())
        diagnostics->activation(message, wparam);
}

void TdurWindowDiagnosticsMessage(
    HWND window, UINT message, WPARAM wparam, LPARAM lparam,
    const tdur::mod::MessageContext& context) noexcept {
    if (auto* diagnostics = tdur::mod::ActiveWindowDiagnostics())
        diagnostics->message(window, message, wparam, lparam, context);
}

void TdurWindowDiagnosticsPresentRequest(
    const char* call, const D3DPRESENT_PARAMETERS& parameters,
    const void* caller) noexcept {
    if (auto* diagnostics = tdur::mod::ActiveWindowDiagnostics())
        diagnostics->present_request(
            call, tdur::mod::DescribePresentRequest(parameters), caller);
}
