#include "tdur/game/fixed_step.hpp"
#include "tdur/mod/display_backend.hpp"

#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace {
using namespace tdur::mod;
using Microsoft::WRL::ComPtr;
unsigned checks = 0;

void Require(bool condition, const char* message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}

void Checked(HRESULT result, const char* message) {
    if (FAILED(result))
        std::cerr << message << " HRESULT=0x" << std::hex
                  << static_cast<unsigned long>(result) << std::dec << "\n";
    Require(SUCCEEDED(result), message);
}

std::uint64_t window_position_messages = 0;

LRESULT CALLBACK WitnessWindowProcedure(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam) {
    if (message == WM_WINDOWPOSCHANGING)
        ++window_position_messages;
    return DefWindowProcW(window, message, wparam, lparam);
}

struct Window {
    HWND value = nullptr;
    Window() {
        WNDCLASSW klass{};
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpfnWndProc = WitnessWindowProcedure;
        klass.lpszClassName = L"TDUR.DisplayBackend.Witness.v1";
        Require(RegisterClassW(&klass) != 0 ||
                    GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
                "register witness window");
        value = CreateWindowExW(0, klass.lpszClassName,
                                L"TDU reconstruction - display API witness",
                                WS_OVERLAPPEDWINDOW, 80, 100, 480, 320, nullptr,
                                nullptr, klass.hInstance, nullptr);
        Require(value != nullptr, "create hidden witness window");
    }
    ~Window() {
        if (value)
            DestroyWindow(value);
    }
};

bool SameRectangle(RECT a, RECT b) {
    return a.left == b.left && a.top == b.top && a.right == b.right &&
           a.bottom == b.bottom;
}

void PolicyTest() {
    DisplayPreferences p;
    Require(p.mode == WindowMode::borderless, "default borderless");
    Require(p.maximum_reference_quality,
            "reference max capabilities requested");
    Require(EffectiveFrameCap(p, 60000.0 / 1001.0) == 60000.0 / 1001.0,
            "fractional native cap retained");
    p.cap = FrameCapMode::native_minus_three;
    Require(EffectiveFrameCap(p, 160) == 157, "optional margin");
    Require(EffectiveFrameCap(p, 2) == 1, "margin lower bound");
    p.cap = FrameCapMode::unlimited;
    Require(EffectiveFrameCap(p, 0) == 0, "unlimited independent of monitor");
    p.cap = FrameCapMode::fixed;
    p.fixed_fps = 144;
    Require(EffectiveFrameCap(p, 60) == 144, "explicit cap");
    for (const double invalid :
         {0.0, -1.0, 1001.0, std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::quiet_NaN()}) {
        p.fixed_fps = invalid;
        bool rejected = false;
        try {
            (void)EffectiveFrameCap(p, 160);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        Require(rejected, "invalid cap rejected");
    }
    for (unsigned bits = 0; bits < 16; ++bits) {
        const bool exclusive = (bits & 1) != 0;
        const bool support = (bits & 2) != 0;
        const bool allow = (bits & 4) != 0;
        const bool vsync = (bits & 8) != 0;
        const auto policy =
            SelectPresentationPolicy(exclusive, support, allow, vsync);
        Require(policy.sync_interval == (vsync ? 1U : 0U), "VSync interval");
        const bool eligible = !exclusive && support && allow && !vsync;
        Require(policy.vrr_eligible == eligible, "conditional VRR eligibility");
        Require(policy.present_flags ==
                    (eligible ? DXGI_PRESENT_ALLOW_TEARING : 0U),
                "tearing flags restricted to supported windowed sync0");
    }
}

void WindowTest() {
    Window window;
    RECT original{};
    Require(GetWindowRect(window.value, &original),
            "original window rectangle");
    const auto originalStyle = GetWindowLongPtrW(window.value, GWL_STYLE);
    const auto originalForeground = GetForegroundWindow();
    const auto monitors = DisplayBackend::monitors();
    Require(!monitors.empty(), "at least one monitor");
    {
        DisplayPreferences preferences;
        DisplayBackend display(window.value, preferences);
        Require(display.render_window() != window.value,
                "separate render sink HWND");
        Require(!IsWindowVisible(display.render_window()),
                "sink remains hidden");
        for (const auto& monitor : monitors) {
            display.move_to_monitor(monitor.handle);
            const auto state = display.state();
            Require(state.monitor.handle == monitor.handle,
                    "actual monitor migration");
            Require(state.monitor.refresh_hz() > 1,
                    "monitor refresh available");
            Require(state.effective_cap_hz == state.monitor.refresh_hz(),
                    "native cap follows actual monitor after migration");
            Require(state.requested_mode == WindowMode::borderless &&
                        !state.exclusive_active,
                    "borderless distinct from exclusive");
            RECT rectangle{};
            Require(GetWindowRect(window.value, &rectangle),
                    "borderless rectangle");
            Require(SameRectangle(rectangle, monitor.bounds),
                    "borderless covers monitor pixels");
            Require((GetWindowLongPtrW(window.value, GWL_STYLE) &
                     (WS_CAPTION | WS_THICKFRAME)) == 0,
                    "borderless has no decoration");
            Require(!IsWindowVisible(window.value),
                    "mode change does not show hidden window");
            std::wcout << L"monitor=" << monitor.gdi_name << L" refresh="
                       << monitor.refresh_numerator << L"/"
                       << monitor.refresh_denominator << L" rational="
                       << monitor.rational_refresh_observed
                       << L" clone_ambiguous="
                       << monitor.cloned_source_ambiguous << L"\n";
            display.set_mode(WindowMode::windowed);
            Require((GetWindowLongPtrW(window.value, GWL_STYLE) &
                     WS_OVERLAPPEDWINDOW) == WS_OVERLAPPEDWINDOW,
                    "windowed decoration restored");
            Require(!display.state().exclusive_active, "windowed DXGI state");
            display.set_mode(WindowMode::borderless);
        }
        Require(GetForegroundWindow() == originalForeground,
                "mode changes do not steal focus");
        Require(!display.state().window_has_foreground,
                "actual witness has no focus");
        Require(!display.state().process_has_foreground,
                "witness process is not foreground");
        bool exclusiveRejected = false;
        try {
            display.set_mode(WindowMode::exclusive);
        } catch (const DisplayError&) {
            exclusiveRejected = true;
        }
        Require(exclusiveRejected,
                "exclusive cannot be faked without a swapchain");
        Require(!display.state().exclusive_active,
                "exclusive failure remains explicit");
    }
    Require(GetWindowLongPtrW(window.value, GWL_STYLE) == originalStyle,
            "original style restored by destructor");
    RECT restored{};
    Require(GetWindowRect(window.value, &restored), "restored rectangle");
    Require(SameRectangle(restored, original), "original placement restored");
    Require(GetForegroundWindow() == originalForeground,
            "restoration does not steal focus");
}

void BorderlessRecoveryTest() {
    Window window;
    const auto foreground = GetForegroundWindow();
    DisplayPreferences preferences;
    preferences.cap = FrameCapMode::unlimited;
    DisplayBackend display(window.value, preferences);
    const auto monitor = display.state().monitor;
    // Simulate a host resizing/restyling its HWND after CreateDevice or Reset.
    SetWindowLongPtrW(window.value, GWL_STYLE, WS_OVERLAPPEDWINDOW);
    Require(SetWindowPos(window.value, nullptr, monitor.bounds.left + 10,
                         monitor.bounds.top + 10, 320, 240,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED),
            "resize owned borderless host");
    display.wait_for_frame();
    RECT actual{};
    Require(GetWindowRect(window.value, &actual), "read recovered rectangle");
    Require(SameRectangle(actual, monitor.bounds),
            "borderless recovers monitor bounds after host resize");
    Require((GetWindowLongPtrW(window.value, GWL_STYLE) &
             (WS_POPUP | WS_CAPTION | WS_THICKFRAME)) == WS_POPUP,
            "borderless recovers undecorated style");
    Require(!IsWindowVisible(window.value) &&
                GetForegroundWindow() == foreground,
            "recovery neither shows nor focuses owned hidden host");
    const auto messages = window_position_messages;
    for (unsigned index = 0; index != 10; ++index)
        display.wait_for_frame();
    Require(window_position_messages == messages,
            "stable borderless frames do not reposition the window");

    display.set_mode(WindowMode::windowed);
    Require(SetWindowPos(window.value, nullptr, monitor.bounds.left + 30,
                         monitor.bounds.top + 30, 360, 260,
                         SWP_NOZORDER | SWP_NOACTIVATE),
            "resize owned windowed host");
    RECT resized{};
    Require(GetWindowRect(window.value, &resized), "read windowed resize");
    const auto windowedMessages = window_position_messages;
    display.wait_for_frame();
    Require(GetWindowRect(window.value, &actual) &&
                SameRectangle(actual, resized) &&
                window_position_messages == windowedMessages,
            "windowed mode preserves a host resize");
}

void PaceAndUnfocusedHostTest() {
    Window window;
    DisplayPreferences preferences;
    preferences.mode = WindowMode::windowed;
    preferences.cap = FrameCapMode::fixed;
    preferences.fixed_fps = 60;
    DisplayBackend display(window.value, preferences);
    auto made = tdur::game::FixedStepScheduler::create({90, 60});
    Require(made.has_value(), "create independent fixed-step scheduler");
    auto scheduler = *made;
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto previous = start;
    uint64_t networkPhase = 0;
    uint64_t uiPhase = 0;
    for (unsigned frame = 0; frame < 61; ++frame) {
        display.wait_for_frame();
        const auto now = Clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(now -
                                                                 previous);
        Require(scheduler.advance(elapsed, false).has_value(),
                "advance unfocused host time");
        previous = now;
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        ++uiPhase;
        // A source-owned network service phase; this counter does not claim
        // to exercise retail network traffic or its internal simulation.
        ++networkPhase;
        while (scheduler.next_tick()) {
        }
        Require(!display.state().window_has_foreground,
                "window remains genuinely unfocused");
    }
    const double elapsed =
        std::chrono::duration<double>(Clock::now() - start).count();
    const auto snapshot = scheduler.snapshot();
    Require(elapsed >= 0.90 && elapsed <= 1.50,
            "fixed-deadline 60 Hz wall-time tolerance");
    Require(snapshot.physics_ticks >= 80 && snapshot.logic_ticks >= 53,
            "host simulation advances without focus");
    Require(snapshot.caught_up(), "no discarded simulation debt");
    Require(networkPhase == 61 && uiPhase == 61,
            "UI and network phases not skipped");
    std::cout << "unfocused_host seconds=" << elapsed
              << " physics=" << snapshot.physics_ticks
              << " logic=" << snapshot.logic_ticks << " ui=" << uiPhase
              << " network_phase=" << networkPhase << "\n";
}

constexpr std::array<D3DCOLOR, 4> Colors{
    D3DCOLOR_ARGB(255, 17, 34, 51), D3DCOLOR_ARGB(255, 68, 85, 102),
    D3DCOLOR_ARGB(255, 119, 136, 153), D3DCOLOR_ARGB(255, 170, 187, 204)};

void Pattern(IDirect3DDevice9* device, UINT width, UINT height,
             unsigned phase) {
    Checked(device->BeginScene(), "BeginScene reference");
    for (unsigned index = 0; index < 4; ++index) {
        const LONG left = (index & 1) ? static_cast<LONG>(width / 2) : 0;
        const LONG right = (index & 1) ? static_cast<LONG>(width)
                                       : static_cast<LONG>(width / 2);
        const LONG top = (index & 2) ? static_cast<LONG>(height / 2) : 0;
        const LONG bottom = (index & 2) ? static_cast<LONG>(height)
                                        : static_cast<LONG>(height / 2);
        const D3DRECT rectangle{left, top, right, bottom};
        Checked(device->Clear(1, &rectangle, D3DCLEAR_TARGET,
                              Colors[(index + phase) % Colors.size()], 1, 0),
                "D3D9 clear quadrant");
    }
    Checked(device->EndScene(), "EndScene before interop");
}

void CheckPattern(const DisplayPixels& pixels, UINT width, UINT height,
                  unsigned phase) {
    Require(pixels.width == width && pixels.height == height,
            "readback dimensions");
    Require(pixels.rgba.size() == static_cast<size_t>(width) * height * 4,
            "readback byte count");
    for (UINT y = 0; y < height; ++y) {
        for (UINT x = 0; x < width; ++x) {
            const unsigned quadrant =
                (x >= width / 2 ? 1U : 0U) + (y >= height / 2 ? 2U : 0U);
            const D3DCOLOR color = Colors[(quadrant + phase) % Colors.size()];
            const auto* rgba =
                pixels.rgba.data() + (static_cast<size_t>(y) * width + x) * 4;
            const std::array<unsigned, 4> expected{
                (color >> 16) & 255U, (color >> 8) & 255U, color & 255U, 255};
            for (size_t channel = 0; channel < expected.size(); ++channel) {
                if (std::to_integer<unsigned>(rgba[channel]) !=
                    expected[channel]) {
                    std::cerr
                        << "pixel mismatch x=" << x << " y=" << y
                        << " channel=" << channel
                        << " got=" << std::to_integer<unsigned>(rgba[channel])
                        << " expected=" << expected[channel] << "\n";
                    Require(false, "D3D9 to D3D12 pixel mismatch");
                }
            }
        }
    }
    Require(true, "all independent quadrant pixels match");
}

void InteropTest(bool maximumQuality) {
    Window window;
    DisplayPreferences preferences;
    preferences.mode = WindowMode::windowed;
    preferences.cap = FrameCapMode::unlimited;
    preferences.maximum_reference_quality = maximumQuality;
    DisplayBackend display(window.value, preferences);
    constexpr UINT width = 70;
    constexpr UINT height = 38;
    display.create_reference_device(width, height);
    const auto state = display.state();
    Require(state.d3d9on12_verified && display.device12() != nullptr,
            "real IDirect3DDevice9On12 and ID3D12Device obtained");
    if (!maximumQuality)
        Require(state.reference_msaa_samples == 1,
                "explicit non-MSAA reference");
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<IDXGIFactory5> factory;
    Checked(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)),
            "fixture DXGI factory");
    const auto luid = display.device12()->GetAdapterLuid();
    Checked(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)),
            "real adapter by LUID");
    DXGI_ADAPTER_DESC1 description{};
    Checked(adapter->GetDesc1(&description), "real adapter description");
    std::wcout << L"D3D9On12 adapter=" << description.Description
               << L" feature_level=>=11_0 msaa=" << state.reference_msaa_samples
               << L" max_anisotropy=" << state.maximum_anisotropy
               << L" tearing_supported=" << state.tearing_supported << L"\n";
    for (unsigned phase = 0; phase < 4; ++phase) {
        Pattern(display.reference_device(), width, height, phase);
        const auto pixels =
            display.readback_surface9(display.reference_target());
        CheckPattern(pixels, width, height, phase);
        const HRESULT result =
            display.present_surface9(display.reference_target());
        Require(SUCCEEDED(result),
                "actual D3D12 flip presentation or occlusion status");
        std::cout << "present_result=0x" << std::hex
                  << static_cast<unsigned long>(result) << std::dec << "\n";
        // This reuse after ReturnUnderlyingResource catches missing fences or
        // stale descriptor/resource lifetimes on the next independently drawn
        // frame.
    }
    const auto presented = display.state();
    Require(!presented.exclusive_active,
            "windowed presentation remains nonexclusive");
    Require(presented.last_present.sync_interval == 0, "explicit sync0 used");
    Require(presented.last_present.vrr_eligible == presented.tearing_supported,
            "reported VRR eligibility reflects actual device feature query");
    Require(presented.last_present.present_flags ==
                (presented.tearing_supported ? DXGI_PRESENT_ALLOW_TEARING : 0U),
            "actual Present flags match feature query");
    RECT beforeFailure{};
    Require(GetWindowRect(window.value, &beforeFailure),
            "rectangle before refused exclusive");
    const LONG_PTR styleBeforeFailure =
        GetWindowLongPtrW(window.value, GWL_STYLE);
    bool exclusiveRejected = false;
    try {
        display.set_mode(WindowMode::exclusive);
    } catch (const DisplayError& error) {
        exclusiveRejected =
            error.result() == DXGI_ERROR_NOT_CURRENTLY_AVAILABLE;
    }
    Require(exclusiveRejected, "no exclusive ownership silently requested for "
                               "an unfocused hidden window");
    const auto afterFailure = display.state();
    Require(!afterFailure.exclusive_active &&
                afterFailure.requested_mode == WindowMode::windowed,
            "refused exclusive preserves actual/requested windowed state");
    RECT afterRectangle{};
    Require(GetWindowRect(window.value, &afterRectangle),
            "rectangle after refused exclusive");
    Require(SameRectangle(beforeFailure, afterRectangle) &&
                GetWindowLongPtrW(window.value, GWL_STYLE) ==
                    styleBeforeFailure,
            "refused exclusive preserves rectangle and style");
    bool nullSurfaceRejected = false;
    try {
        (void)display.present_surface9(nullptr);
    } catch (const std::invalid_argument&) {
        nullSurfaceRejected = true;
    }
    Require(nullSurfaceRejected,
            "null surface rejected before recording GPU transitions");
    ComPtr<IDirect3DSurface9> unsupported;
    Checked(display.reference_device()->CreateRenderTarget(
                width, height, D3DFMT_R5G6B5, D3DMULTISAMPLE_NONE, 0, FALSE,
                &unsupported, nullptr),
            "fixture unsupported-format target");
    bool formatRejected = false;
    try {
        (void)display.present_surface9(unsupported.Get());
    } catch (const DisplayError& error) {
        formatRejected = error.result() == DXGI_ERROR_UNSUPPORTED;
    }
    Require(formatRejected,
            "unsupported source format rejected before target barriers");
    display.set_mode(WindowMode::borderless);
    Pattern(display.reference_device(), width, height, 1);
    Require(SUCCEEDED(display.present_surface9(display.reference_target())),
            "present after borderless ResizeBuffers");
    display.set_mode(WindowMode::windowed);
    Pattern(display.reference_device(), width, height, 2);
    Require(SUCCEEDED(display.present_surface9(display.reference_target())),
            "present after windowed ResizeBuffers");
}

void ClassicManagedTest() {
    Window window;
    DisplayPreferences preferences;
    preferences.mode = WindowMode::windowed;
    preferences.cap = FrameCapMode::unlimited;
    DisplayBackend display(window.value, preferences);
    auto* factory = display.factory9on12_compat();
    D3DPRESENT_PARAMETERS parameters{};
    parameters.BackBufferWidth = 64;
    parameters.BackBufferHeight = 32;
    parameters.BackBufferFormat = D3DFMT_A8R8G8B8;
    parameters.BackBufferCount = 1;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = display.render_window();
    parameters.Windowed = TRUE;
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    ComPtr<IDirect3DDevice9> device;
    Checked(factory->CreateDevice(
                D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window.value,
                D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
                &parameters, &device),
            "classic Direct3DCreate9On12 / CreateDevice");
    display.attach_device9(device.Get());
    Require(display.state().d3d9on12_verified,
            "classic device really backed by D3D12");
    ComPtr<IDirect3DSurface9> target;
    Checked(device->GetRenderTarget(0, &target), "classic backbuffer surface");
    ComPtr<IDirect3DTexture9> managed;
    Checked(device->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                  &managed, nullptr),
            "D3DPOOL_MANAGED texture supported");
    D3DLOCKED_RECT locked{};
    Checked(managed->LockRect(0, &locked, nullptr, 0), "managed texture lock");
    constexpr D3DCOLOR color = D3DCOLOR_ARGB(255, 13, 101, 229);
    for (unsigned row = 0; row < 4; ++row) {
        auto* pixels = reinterpret_cast<D3DCOLOR*>(
            static_cast<std::byte*>(locked.pBits) +
            static_cast<size_t>(row) * locked.Pitch);
        for (unsigned column = 0; column < 4; ++column)
            pixels[column] = color;
    }
    Checked(managed->UnlockRect(0), "managed texture unlock");
    struct Vertex {
        float x, y, z, rhw, u, v;
    };
    const std::array<Vertex, 4> quad{{{-0.5f, -0.5f, 0.5f, 1, 0, 0},
                                      {63.5f, -0.5f, 0.5f, 1, 1, 0},
                                      {-0.5f, 31.5f, 0.5f, 1, 0, 1},
                                      {63.5f, 31.5f, 0.5f, 1, 1, 1}}};
    Checked(device->SetRenderState(D3DRS_ZENABLE, FALSE), "classic depth off");
    Checked(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE),
            "classic cull off");
    Checked(device->SetRenderState(D3DRS_LIGHTING, FALSE),
            "classic lighting off");
    Checked(device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1),
            "classic select texture color");
    Checked(device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE),
            "classic texture argument");
    Checked(device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1),
            "classic texture alpha");
    Checked(device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE),
            "classic alpha argument");
    Checked(device->SetTexture(0, managed.Get()),
            "classic bind managed texture");
    Checked(device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1),
            "classic transformed vertex declaration");
    Checked(device->BeginScene(), "classic begin scene");
    Checked(device->Clear(0, nullptr, D3DCLEAR_TARGET,
                          D3DCOLOR_ARGB(255, 255, 0, 255), 1, 0),
            "classic distinguishable clear");
    Checked(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad.data(),
                                    sizeof(Vertex)),
            "classic draw using real managed texture");
    Checked(device->EndScene(), "classic end before checkout");
    const auto result = display.readback_surface9(target.Get());
    Require(result.width == 64 && result.height == 32,
            "classic GPU readback dimensions");
    for (size_t pixel = 0; pixel < 64 * 32; ++pixel) {
        const auto* rgba = result.rgba.data() + pixel * 4;
        Require(std::to_integer<unsigned>(rgba[0]) == 13 &&
                    std::to_integer<unsigned>(rgba[1]) == 101 &&
                    std::to_integer<unsigned>(rgba[2]) == 229 &&
                    std::to_integer<unsigned>(rgba[3]) == 255,
                "classic managed texture draw verified through real D3D12 "
                "readback");
    }
    Require(SUCCEEDED(display.present_surface9(target.Get())),
            "classic backbuffer presented through D3D12");
    std::cout << "classic_managed_draw verified_pixels=2048\n";
}

void VisibleExclusiveTest() {
    Window window;
    SetWindowTextW(window.value,
                   L"TDU reconstruction - témoin D3D9On12 (aucun jeu)");
    DisplayPreferences preferences;
    preferences.mode = WindowMode::borderless;
    preferences.maximum_reference_quality = false;
    DisplayBackend display(window.value, preferences);
    display.create_reference_device(70, 38);
    Pattern(display.reference_device(), 70, 38, 0);
    // Only this explicitly selected fixture may request foreground. The
    // backend itself never fakes activation or captures global input.
    ShowWindow(window.value, SW_SHOW);
    SetForegroundWindow(window.value);
    using Clock = std::chrono::steady_clock;
    const auto visibleStart = Clock::now();
    const auto beforeExclusive = display.state();
    RECT beforeRectangle{};
    GetWindowRect(window.value, &beforeRectangle);
    std::cout << "exclusive_attempt foreground_window="
              << beforeExclusive.window_has_foreground << " foreground_process="
              << beforeExclusive.process_has_foreground
              << " visible=" << IsWindowVisible(window.value) << "\n";
    try {
        display.set_mode(WindowMode::exclusive);
    } catch (const DisplayError& error) {
        RECT afterRectangle{};
        GetWindowRect(window.value, &afterRectangle);
        const auto restored = display.state();
        Require(!restored.exclusive_active &&
                    restored.requested_mode == WindowMode::borderless,
                "exclusive refusal leaves honest borderless state");
        Require(SameRectangle(beforeRectangle, afterRectangle),
                "exclusive refusal restores exact window rectangle");
        Checked(display.present_surface9(display.reference_target()),
                "borderless still renders after exclusive refusal");
        ShowWindow(window.value, SW_HIDE);
        std::cout << "exclusive_refused_hresult=0x" << std::hex
                  << static_cast<unsigned long>(error.result()) << std::dec
                  << " rollback_verified=1\n";
        throw;
    }
    const auto exclusive = display.state();
    Require(exclusive.exclusive_active,
            "real DXGI exclusive ownership verified");
    Require(exclusive.requested_mode == WindowMode::exclusive,
            "exclusive request tracked");
    uint64_t presents = 0;
    while (Clock::now() - visibleStart < std::chrono::milliseconds(1300)) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        display.wait_for_frame();
        Checked(display.present_surface9(display.reference_target()),
                "actual exclusive D3D12 Present");
        const auto policy = display.state().last_present;
        Require(policy.present_flags == 0,
                "no windowed tearing flag in exclusive");
        Require(
            !policy.vrr_eligible,
            "exclusive VRR eligibility not inferred from windowed DXGI query");
        ++presents;
    }
    display.set_mode(WindowMode::borderless);
    Require(!display.state().exclusive_active, "exclusive ownership released");
    Checked(display.present_surface9(display.reference_target()),
            "return to borderless D3D12 Present");
    display.set_mode(WindowMode::windowed);
    Require(!display.state().exclusive_active, "return to true windowed state");
    Checked(display.present_surface9(display.reference_target()),
            "return to windowed D3D12 Present");
    ShowWindow(window.value, SW_HIDE);
    const double visibleSeconds =
        std::chrono::duration<double>(Clock::now() - visibleStart).count();
    std::cout << "visible_exclusive presents=" << presents
              << " visible_seconds=" << visibleSeconds
              << " desktop_refresh=" << exclusive.monitor.refresh_numerator
              << "/" << exclusive.monitor.refresh_denominator << "\n";
    Require(visibleSeconds < 2,
            "visible exclusive fixture bounded to two seconds");
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool windowsOnly =
            argc > 1 && std::string_view(argv[1]) == "--window-only";
        const bool interopOnly =
            argc > 1 && std::string_view(argv[1]) == "--interop-only";
        const bool visibleExclusive =
            argc > 1 && std::string_view(argv[1]) == "--exclusive-visible";
        SetProcessDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        if (visibleExclusive) {
            VisibleExclusiveTest();
        } else if (!interopOnly) {
            PolicyTest();
            WindowTest();
            BorderlessRecoveryTest();
            PaceAndUnfocusedHostTest();
        }
        if (!visibleExclusive && !windowsOnly) {
            InteropTest(false);
            InteropTest(true);
            ClassicManagedTest();
        }
        std::cout << "display_api_witness checks=" << checks << " failures=0\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "display_api_witness checks=" << checks
                  << " failed: " << error.what() << "\n";
        return 1;
    }
}
