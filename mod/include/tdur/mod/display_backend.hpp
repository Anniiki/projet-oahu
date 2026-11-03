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
