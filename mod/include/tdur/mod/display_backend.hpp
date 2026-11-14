#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d12.h>
#include <d3d9.h>
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace tdur::mod {

enum class WindowMode { windowed, borderless, exclusive };
enum class FrameCapMode {
    native_refresh,
    native_minus_three,
    fixed,
    unlimited
};

struct DisplayPreferences {
    WindowMode mode = WindowMode::borderless;
    FrameCapMode cap = FrameCapMode::native_refresh;
    double fixed_fps = 60;
    uint32_t window_width = 1280;
    uint32_t window_height = 720;
    bool vsync = false;
    bool allow_tearing = true;
    // A request for supported reference-renderer capabilities. It does not
    // claim to alter retail game quality variables or original simulation.
    bool maximum_reference_quality = true;
    // Constrain the visible HWND in borderless mode before Windows applies a
    // foreign size, position or style change (see constrain_window_message).
    bool constrain_borderless = true;
    // Optional owner-thread log of bounded borderless corrections. Each text
    // is "<event> <key=value ...>": sizes, kinds and counters only, never
    // coordinates or handles.
    void (*log)(const char* event, void* context) noexcept = nullptr;
    void* log_context = nullptr;
};

// Exact on/off switch read from an environment variable. value/length/
// last_error come from GetEnvironmentVariableW: length 0 with
// ERROR_ENVVAR_NOT_FOUND is absent, "1" enabled, "0" disabled, anything
// else (empty, longer, other text, other error) invalid.
enum class SwitchSetting : std::uint8_t { absent, enabled, disabled, invalid };
[[nodiscard]] SwitchSetting ParseSwitchSetting(const wchar_t* value,
                                               DWORD length,
                                               DWORD last_error) noexcept;

struct BorderlessCorrectionCounters {
    std::uint64_t constrained{}, repaired{}, suspended{}, storms{},
        migrations{}, skipped{};
};

// Shared bound for borderless constraints (C2) and repairs. A storm is more
// than 4 corrections, constraints and repairs together, within less than
// 1,000 ms (ring of the last 5 instants). The correction that crosses the
// threshold opens an episode; repairs are suspended for the next 1,000 ms.
// A constraint is always applied (apply, or apply_storm when it opens the
// episode); a repair that opens the episode or falls inside it is
// suspended. The end of an episode empties the ring, so the first repair
// after it is applied even under a cause that never stops: the host size is
// kept at most 1,000 ms per episode. There is no permanent disable. Pure:
// the clock is injected. skipped counts constraints C2 could not evaluate.
class BorderlessCorrectionGuard {
  public:
    enum class Kind : std::uint8_t { constrained, repaired, migration };
    enum class Verdict : std::uint8_t { apply, apply_storm, suspend };
    static constexpr unsigned storm_corrections = 5;
    static constexpr std::uint64_t storm_window_ms = 1000;
    static constexpr std::uint64_t suspension_ms = 1000;
    static constexpr std::uint64_t summary_interval_ms = 5000;
    static constexpr unsigned individual_lines = 16;
    static constexpr unsigned storm_lines = 16;
    static constexpr unsigned summary_lines = 32;

    Verdict record(Kind kind, std::uint64_t now_ms) noexcept;
    // A constraint C2 had to abstain from (no rectangle, no monitor).
    void record_skipped() noexcept { ++counters_.skipped; }
    // True when the last record() opened an episode (one storm line).
    [[nodiscard]] bool opened_episode() const noexcept { return opened_; }
    // Line budgets: 16 individual and 16 storm lines per backend. The
    // counters of every correction stay in the summaries.
    [[nodiscard]] bool take_individual_line() noexcept;
    [[nodiscard]] bool take_storm_line() noexcept;
    // Only when a counter changed since the last summary, the first 5 s after
    // the first correction. The first 32 summaries are at least 5 s apart;
    // after them the interval doubles each time (10 s, 20 s, 40 s...), so a
    // lasting cause is never silent and the count stays logarithmic.
    [[nodiscard]] bool summary_due(std::uint64_t now_ms) noexcept;
    // True once, right after the 32nd summary: the spacing starts doubling.
    [[nodiscard]] bool take_sparse_notice() noexcept;
    // The last summary, at backend destruction, if a counter changed since
    // the previous one. Outside every budget.
    [[nodiscard]] bool final_summary_due() noexcept;
    [[nodiscard]] const BorderlessCorrectionCounters&
    counters() const noexcept {
        return counters_;
    }

  private:
    [[nodiscard]] bool changed() const noexcept;

    BorderlessCorrectionCounters counters_{};
    BorderlessCorrectionCounters summarized_{};
    std::uint64_t ring_[storm_corrections]{};
    unsigned ring_count_{};
    unsigned ring_next_{};
    std::uint64_t suspended_until_{};
    bool episode_{};
    bool opened_{};
    bool anchored_{};
    std::uint64_t last_summary_ms_{};
    unsigned individual_left_ = individual_lines;
    unsigned storm_left_ = storm_lines;
    unsigned summaries_{};
    bool sparse_noticed_{};
};

struct MonitorState {
    HMONITOR handle = nullptr;
    RECT bounds{};
    RECT work_area{};
    std::wstring gdi_name;
    uint32_t refresh_numerator = 60;
    uint32_t refresh_denominator = 1;
    bool rational_refresh_observed = false;
    bool cloned_source_ambiguous = false;
    double refresh_hz() const noexcept;
};

struct PresentationPolicy {
    uint32_t sync_interval{};
    uint32_t present_flags{};
    bool vrr_eligible = false;
};

struct DisplayState {
    MonitorState monitor;
    WindowMode requested_mode = WindowMode::borderless;
    bool exclusive_active = false;
    bool exclusive_transition_pending = false;
    bool tearing_supported = false;
    bool window_has_foreground = false;
    bool process_has_foreground = false;
    bool d3d9on12_verified = false;
    double effective_cap_hz{};
    uint32_t maximum_anisotropy{};
    uint32_t reference_msaa_samples = 1;
    HRESULT backend_failure = S_OK;
    bool retained_gpu_submission = false;
    PresentationPolicy last_present;
    // DXGI exposes eligibility, not a reliable physical VRR-active bit.
    // No field here reports the retail simulation as running from focus alone.
};

struct DisplayPixels {
    uint32_t width{};
    uint32_t height{};
    std::vector<std::byte> rgba;
};

class DisplayError : public std::runtime_error {
  public:
    DisplayError(HRESULT result, const char* operation);
    HRESULT result() const noexcept { return result_; }

  private:
    HRESULT result_;
};

[[nodiscard]] double EffectiveFrameCap(const DisplayPreferences&,
                                       double refreshHz);
[[nodiscard]] PresentationPolicy SelectPresentationPolicy(bool exclusiveActive,
                                                          bool tearingSupported,
                                                          bool allowTearing,
                                                          bool vsync) noexcept;

// Brackets the call of the host's own (native) handler inside an HWND
// procedure that forwards messages to constrain_window_message: one scope
// around DefSubclassProc, or around the host code of a test procedure. A
// message received while the host handler of an outer message is running
// comes from a call made by that handler, never from the bridge itself.
class DisplayHostHandlerScope {
  public:
    DisplayHostHandlerScope() noexcept;
    ~DisplayHostHandlerScope();
    DisplayHostHandlerScope(const DisplayHostHandlerScope&) = delete;
    DisplayHostHandlerScope& operator=(const DisplayHostHandlerScope&) = delete;
};

// True on the owner thread at the level of a window operation of the bridge
// itself (style, placement, mode, monitor): the operation and the messages
// Windows sends synchronously for it. False outside any operation and for
// the messages of a call the host handler makes meanwhile (one or more
// DisplayHostHandlerScope deeper than the operation).
[[nodiscard]] bool DisplayWindowOperationActive() noexcept;

// One owned reference, like Direct3DCreate9; call outside DllMain. No HWND is
// needed and no classic-D3D9 fallback is used. Runtime stays process-resident.
[[nodiscard]] IDirect3D9* CreateFactory9On12(UINT sdkVersion = D3D_SDK_VERSION);

// Public Win32/D3D9On12/DXGI infrastructure, never an internal game patch.
// Construct and manage modes on the owning HWND thread, outside DllMain.
// Device access/presentation are synchronous and must not be used concurrently.
class DisplayBackend {
  public:
    explicit DisplayBackend(HWND window, DisplayPreferences preferences = {});
    ~DisplayBackend();
    DisplayBackend(const DisplayBackend&) = delete;
    DisplayBackend& operator=(const DisplayBackend&) = delete;

    void set_mode(WindowMode mode);
    void move_to_monitor(HMONITOR monitor);

    struct WindowConstraint {
        bool changed = false;
        bool position = false, style = false, extended_style = false;
        long requested_width = 0, requested_height = 0;
    };
    // Owner thread, from the HWND procedure after the native handler (and
    // after its DisplayHostHandlerScope), for WM_WINDOWPOSCHANGING and
    // WM_STYLECHANGING. In borderless mode, with constrain_borderless and
    // outside minimization, a foreign size, position or decoration is
    // replaced by the borderless geometry before Windows applies it. The
    // bridge's own windowed, exclusive, restoring and monitor operations are
    // exempt at their level only: a call the host handler makes meanwhile is
    // constrained when the operation targets borderless, and so is a host
    // change written into the bridge's own borderless message. A target
    // equal to another monitor's exact bounds is left to the shell. Never
    // calls SetWindowPos, never throws, never shows, minimizes or activates.
    WindowConstraint constrain_window_message(UINT message, WPARAM wparam,
                                              LPARAM lparam) noexcept;
    [[nodiscard]] BorderlessCorrectionCounters corrections() const noexcept;
    void refresh_monitor();
    [[nodiscard]] DisplayState state();
    void wait_for_frame();

    // An independent render-sink HWND lets the DXGI presenter own the visible
    // HWND. A future retail CreateDevice hook must explicitly use this sink
    // for hDeviceWindow and keep the real HWND as its focus window.
    HWND render_window() const;
    // Use the classic factory for a Direct3DCreate9 caller, retaining managed
    // pool behavior. The Ex factory below is an explicit, separate option.
    IDirect3D9* factory9on12_compat();
    IDirect3D9Ex* factory9on12();
    // Borrowed: caller keeps device alive throughout each operation. No
    // retained device9/interop/default-pool reference blocks Release/Reset.
    void attach_device9(IDirect3DDevice9* device);
    void prepare_device_reset();
    void complete_device_reset();
    // Owner thread during WM_NCDESTROY: suppress restoration of dying HWND.
    void abandon_window() noexcept;

    // Source-owned fixture support; no retail options or save file is changed.
    void create_reference_device(uint32_t width, uint32_t height);
    IDirect3DDevice9Ex* reference_device() const;
    IDirect3DSurface9* reference_target() const;
    ID3D12Device* device12() const;

    // EndScene must have completed. Interop checkout/return and fences protect
    // all D3D9/D3D12 resource usage. Unsupported runtimes fail explicitly.
    [[nodiscard]] DisplayPixels readback_surface9(IDirect3DSurface9* surface);
    HRESULT present_surface9(IDirect3DSurface9* surface);

    static std::vector<MonitorState> monitors();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tdur::mod
