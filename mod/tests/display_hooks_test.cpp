#include "tdur/mod/display_hooks.hpp"

#include <d3d9on12.h>
#include <wrl/client.h>

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace tdur::mod;
using Microsoft::WRL::ComPtr;
unsigned checks = 0;
unsigned logCalls = 0;
unsigned deviceCreatedEvents = 0;
unsigned firstFrameEvents = 0;
unsigned presentedFrameEvents = 0;
bool rejectNextDevice = false;
HWND destroyOnPositionChange = nullptr;
HRESULT DeviceCreated(IDirect3DDevice9* device) noexcept {
    if (device)
        ++deviceCreatedEvents;
    if (rejectNextDevice) {
        rejectNextDevice = false;
        return E_ABORT;
    }
    return S_OK;
}
void FirstFrame() noexcept { ++firstFrameEvents; }
void PresentedFrame() noexcept { ++presentedFrameEvents; }

LRESULT CALLBACK FixtureWindowProcedure(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam) {
    if (message == WM_WINDOWPOSCHANGED && window == destroyOnPositionChange) {
        destroyOnPositionChange = nullptr;
        DestroyWindow(window);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void Require(bool condition, const char* message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
void Check(HRESULT result, const char* message) {
    if (FAILED(result))
        std::cerr << message << " HRESULT=0x" << std::hex
                  << static_cast<unsigned long>(result) << std::dec << "\n";
    Require(SUCCEEDED(result), message);
}
void Log(const char* event, HRESULT result, void*) noexcept {
    ++logCalls;
    std::cout << event << "=0x" << std::hex
              << static_cast<unsigned long>(result) << std::dec << "\n";
}
struct Window {
    HWND value{};
    Window() {
        WNDCLASSW klass{};
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpfnWndProc = FixtureWindowProcedure;
        klass.lpszClassName = L"TDUR.ClassicD3D9.HookWitness.v1";
        Require(RegisterClassW(&klass) != 0 ||
                    GetLastError() == ERROR_CLASS_ALREADY_EXISTS,
                "register fixture window");
        value = CreateWindowExW(
            0, klass.lpszClassName,
            L"TDU reconstruction - classic D3D9 hook witness (no game)",
            WS_OVERLAPPEDWINDOW, 120, 140, 480, 300, nullptr, nullptr,
            klass.hInstance, nullptr);
        Require(value != nullptr, "create hidden fixture HWND");
    }
    ~Window() {
        if (value)
            DestroyWindow(value);
    }
};

ComPtr<IDirect3DTexture9> ManagedTexture(IDirect3DDevice9* device) {
    ComPtr<IDirect3DTexture9> texture;
    Check(device->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                &texture, nullptr),
          "real classic managed texture");
    D3DLOCKED_RECT locked{};
    Check(texture->LockRect(0, &locked, nullptr, 0),
          "lock real managed texture");
    for (unsigned row = 0; row < 4; ++row) {
        auto* pixels = reinterpret_cast<D3DCOLOR*>(
            static_cast<std::byte*>(locked.pBits) +
            static_cast<size_t>(row) * locked.Pitch);
        for (unsigned column = 0; column < 4; ++column)
            pixels[column] = D3DCOLOR_ARGB(255, 23, 117, 209);
    }
    Check(texture->UnlockRect(0), "unlock real managed texture");
    return texture;
}

void Draw(IDirect3DDevice9* device, IDirect3DTexture9* texture, UINT width,
          UINT height) {
    Check(device->SetRenderState(D3DRS_ZENABLE, FALSE),
          "disable fixture depth");
    Check(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE),
          "disable fixture culling");
    Check(device->SetRenderState(D3DRS_LIGHTING, FALSE),
          "disable fixture lighting");
    Check(device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1),
          "select texture color");
    Check(device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE),
          "texture color argument");
    Check(device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1),
          "select texture alpha");
    Check(device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE),
          "texture alpha argument");
    Check(device->SetTexture(0, texture), "bind managed texture");
    Check(device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1), "real D3D9 FVF");
    struct Vertex {
        float x, y, z, rhw, u, v;
    };
    const float right = static_cast<float>(width) - 0.5f;
    const float bottom = static_cast<float>(height) - 0.5f;
    const std::array<Vertex, 4> quad{{{-0.5f, -0.5f, 0.5f, 1, 0, 0},
                                      {right, -0.5f, 0.5f, 1, 1, 0},
                                      {-0.5f, bottom, 0.5f, 1, 0, 1},
                                      {right, bottom, 0.5f, 1, 1, 1}}};
    Check(device->BeginScene(), "BeginScene fixture");
    Check(device->Clear(0, nullptr, D3DCLEAR_TARGET,
                        D3DCOLOR_ARGB(255, 255, 0, 255), 1, 0),
          "clear distinct from expected texture");
    Check(device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad.data(),
                                  sizeof(Vertex)),
          "draw real D3D9 managed quad");
    Check(device->EndScene(), "EndScene before hooked Present");
}

void Pixels(IDirect3DDevice9* device, UINT width, UINT height) {
    DisplayPixels pixels;
    Check(ReadDisplayBackbuffer9(device, pixels),
          "D3D12 readback through registered real device");
    Require(pixels.width == width && pixels.height == height,
            "real Reset dimensions read back");
    for (size_t index = 0; index < static_cast<size_t>(width) * height;
         ++index) {
        const auto* pixel = pixels.rgba.data() + index * 4;
        Require(std::to_integer<unsigned>(pixel[0]) == 23 &&
                    std::to_integer<unsigned>(pixel[1]) == 117 &&
                    std::to_integer<unsigned>(pixel[2]) == 209 &&
                    std::to_integer<unsigned>(pixel[3]) == 255,
                "managed texture pixels exact across D3D9On12/D3D12");
    }
}
} // namespace

int main() {
    try {
        SetProcessDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        DisplayHookOptions options;
        Require(InstallDisplayHooks(options) == S_FALSE,
                "bridge disabled unless explicitly selected");
        Require(!DisplayHooksReport().installed,
                "disabled bridge has no installed hooks");
        Window window;
        RECT originalRectangle{};
        GetWindowRect(window.value, &originalRectangle);
        const auto originalStyle = GetWindowLongPtrW(window.value, GWL_STYLE);
        const auto originalForeground = GetForegroundWindow();
        options.enabled = true;
        options.display.cap = FrameCapMode::fixed;
        options.display.fixed_fps = 240;
        options.log = Log;
        options.on_device_created = DeviceCreated;
        options.on_first_frame = FirstFrame;
        options.on_presented_frame = PresentedFrame;
        Check(InstallDisplayHooks(options),
              "explicit public hooks installed outside loader");
        Require(InstallDisplayHooks(options) ==
                    HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED),
                "duplicate install rejected");
        // A normal import, not a direct call to our factory helper.
        ComPtr<IDirect3D9> factory;
        factory.Attach(Direct3DCreate9(D3D_SDK_VERSION));
        Require(factory != nullptr,
                "normal Direct3DCreate9 import intercepted");
        Require(DisplayHooksReport().factories_on12 == 1,
                "one real factory replacement");
        D3DPRESENT_PARAMETERS parameters{};
        parameters.Windowed = TRUE;
        parameters.hDeviceWindow = window.value;
        parameters.BackBufferWidth = 64;
        parameters.BackBufferHeight = 32;
        parameters.BackBufferFormat = D3DFMT_A8R8G8B8;
        parameters.BackBufferCount = 1;
        parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        DWORD levels = 0;
        if (SUCCEEDED(factory->CheckDeviceMultiSampleType(
                0, D3DDEVTYPE_HAL, parameters.BackBufferFormat, TRUE,
                D3DMULTISAMPLE_4_SAMPLES, &levels)))
            parameters.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES;
        ComPtr<IDirect3DDevice9> device;
        Check(factory->CreateDevice(0, D3DDEVTYPE_HAL, window.value,
                                    D3DCREATE_HARDWARE_VERTEXPROCESSING |
                                        D3DCREATE_FPU_PRESERVE,
                                    &parameters, &device),
              "normal CreateDevice intercepted");
        Require(deviceCreatedEvents == 1 && firstFrameEvents == 0,
                "creation callback before return, before any presentation");
        Require(parameters.hDeviceWindow == window.value,
                "caller public HWND retained");
        D3DDEVICE_CREATION_PARAMETERS creation{};
        Check(device->GetCreationParameters(&creation),
              "native creation parameters");
        Require(creation.hFocusWindow == window.value,
                "real focus HWND preserved");
        DisplayState display;
        Check(DisplayDeviceState9(device.Get(), display),
              "real bridged display state");
        Require(display.d3d9on12_verified &&
                    display.requested_mode == WindowMode::borderless &&
                    !display.exclusive_active,
                "true On12 and honest borderless state");
        Require(!display.window_has_foreground &&
                    !display.process_has_foreground,
                "fixture remains genuinely unfocused");
        ComPtr<IDirect3DDevice9On12> on12;
        Check(device.As(&on12), "QI exposes actual On12 interface");
        ComPtr<ID3D12Device> device12;
        Check(on12->GetD3D12Device(IID_PPV_ARGS(&device12)),
              "native D3D12 device through public QI");
        device12.Reset();
        on12.Reset();
        auto texture = ManagedTexture(device.Get());
        ComPtr<IDirect3DDevice9> fromResource;
        Check(texture->GetDevice(&fromResource),
              "resource GetDevice uses real object");
        ComPtr<IUnknown> identityA;
        ComPtr<IUnknown> identityB;
        Check(device.As(&identityA), "original canonical identity");
        Check(fromResource.As(&identityB), "resource canonical identity");
        Require(identityA.Get() == identityB.Get(),
                "COM identity preserved across GetDevice");
        identityB.Reset();
        Draw(fromResource.Get(), texture.Get(), 64, 32);
        std::cout << "device=" << device.Get()
                  << " resource_device=" << fromResource.Get()
                  << " present_entries="
                  << (*reinterpret_cast<void***>(device.Get()))[17] << "/"
                  << (*reinterpret_cast<void***>(fromResource.Get()))[17]
                  << " tracked=" << DisplayHooksReport().tracked_devices
                  << "\n";
        Check(fromResource->Present(nullptr, nullptr, nullptr, nullptr),
              "GetDevice result still intercepts real Present");
        Require(DisplayHooksReport().presents == 1,
                "GetDevice cannot bypass presentation hook");
        Require(firstFrameEvents == 1,
                "first-frame callback follows successful DXGI presentation");
        Require(presentedFrameEvents == 1,
                "per-frame callback follows successful DXGI presentation");
        Pixels(device.Get(), 64, 32);
        ComPtr<IDirect3DSwapChain9> swap;
        Check(device->GetSwapChain(0, &swap), "real primary swapchain");
        D3DPRESENT_PARAMETERS nativeParameters{};
        Check(swap->GetPresentParameters(&nativeParameters),
              "native render-sink parameters");
        Require(nativeParameters.hDeviceWindow != window.value &&
                    !IsWindowVisible(nativeParameters.hDeviceWindow),
                "separate real hidden render sink");
        Check(swap->Present(nullptr, nullptr, nullptr, nullptr, 0),
              "swapchain Present cannot bypass presentation hook");
        Require(DisplayHooksReport().presents == 2,
                "primary swapchain routed to same presenter");
        Require(firstFrameEvents == 1,
                "ordinary presentation does not repeat the initial event");
        Require(
            presentedFrameEvents == 2,
            "per-frame callback also follows primary swapchain presentation");
        ComPtr<IDirect3DSwapChain9> additional;
        Require(device->CreateAdditionalSwapChain(&parameters, &additional) ==
                        D3DERR_NOTAVAILABLE &&
                    !additional,
                "unsupported additional swapchain refused explicitly");
        Require(UninstallDisplayHooks() == HRESULT_FROM_WIN32(ERROR_BUSY),
                "cannot detach while COM objects are live");
        ComPtr<IDirect3DSurface9> retained;
        Check(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &retained),
              "hold default-pool resource for Reset failure");
        swap.Reset();
        auto reset = parameters;
        reset.BackBufferWidth = 80;
        reset.BackBufferHeight = 40;
        reset.Windowed = FALSE;
        reset.FullScreen_RefreshRateInHz = 160;
        Require(FAILED(device->Reset(&reset)),
                "native Reset fails with outstanding backbuffer reference");
        Require(DisplayHooksReport().resets_failed == 1,
                "failed native Reset recorded");
        retained.Reset();
        Check(device->Reset(&reset),
              "native Reset succeeds after default-pool release");
        Require(reset.hDeviceWindow == window.value && reset.Windowed &&
                    reset.FullScreen_RefreshRateInHz == 0,
                "Reset exposes actual windowed render policy");
        Require(DisplayHooksReport().resets_succeeded == 1,
                "successful native Reset recorded");
        Require(deviceCreatedEvents == 1,
                "Reset never reapplies device-creation preferences");
        Draw(device.Get(), texture.Get(), 80, 40);
        Check(device->Present(nullptr, nullptr, nullptr, nullptr),
              "Present after true Reset");
        Pixels(device.Get(), 80, 40);
        Check(SetDisplayDeviceMode9(device.Get(), WindowMode::windowed),
              "public mode switch windowed");
        Check(device->Present(nullptr, nullptr, nullptr, nullptr),
              "Present after windowed mode");
        Check(SetDisplayDeviceMode9(device.Get(), WindowMode::borderless),
              "public mode switch borderless");
        Check(device->Present(nullptr, nullptr, nullptr, nullptr),
              "Present after borderless mode");
        const RECT unsupportedRect{0, 0, 1, 1};
        Require(device->Present(&unsupportedRect, nullptr, nullptr, nullptr) ==
                    D3DERR_INVALIDCALL,
                "unsupported partial presentation fails explicitly");
        Check(device->Present(nullptr, nullptr, nullptr, nullptr),
              "invalid present did not corrupt next frame");
        Require(GetForegroundWindow() == originalForeground,
                "hook never steals foreground");
        fromResource.Reset();
        texture.Reset();
        Check(device->SetTexture(0, nullptr),
              "unbind managed texture before final release");
        device.Reset();
        Require(DisplayHooksReport().tracked_devices == 1,
                "QI alias keeps native object alive");
        const ULONG finalCount = identityA.Detach()->Release();
        Require(finalCount == 0, "real final Release not pinned by backend");
        Require(DisplayHooksReport().tracked_devices == 0,
                "presenter released on native final Release");
        Require(GetWindowLongPtrW(window.value, GWL_STYLE) == originalStyle,
                "style restored at device lifetime end");
        RECT restored{};
        GetWindowRect(window.value, &restored);
        Require(EqualRect(&restored, &originalRectangle),
                "placement restored at device lifetime end");
        {
            Window rejected;
            auto requested = parameters;
            requested.hDeviceWindow = rejected.value;
            ComPtr<IDirect3DDevice9> rejectedDevice;
            rejectNextDevice = true;
            Require(factory->CreateDevice(0, D3DDEVTYPE_HAL, rejected.value,
                                          D3DCREATE_HARDWARE_VERTEXPROCESSING |
                                              D3DCREATE_FPU_PRESERVE,
                                          &requested,
                                          &rejectedDevice) == E_ABORT &&
                        !rejectedDevice,
                    "policy failure is propagated without exposing a device");
            Require(DisplayHooksReport().tracked_devices == 0,
                    "rejected policy leaves no presenter registry entry");
            Check(factory->CreateDevice(0, D3DDEVTYPE_HAL, rejected.value,
                                        D3DCREATE_HARDWARE_VERTEXPROCESSING |
                                            D3DCREATE_FPU_PRESERVE,
                                        &requested, &rejectedDevice),
                  "clean retry after source-owned policy rejection");
            rejectedDevice.Reset();
            Require(DisplayHooksReport().tracked_devices == 0,
                    "retry device releases its presenter normally");
        }
        {
            Window closing;
            auto parameters2 = parameters;
            parameters2.hDeviceWindow = closing.value;
            ComPtr<IDirect3DDevice9> retainedDevice;
            Check(factory->CreateDevice(0, D3DDEVTYPE_HAL, closing.value,
                                        D3DCREATE_HARDWARE_VERTEXPROCESSING |
                                            D3DCREATE_FPU_PRESERVE,
                                        &parameters2, &retainedDevice),
                  "second real device before HWND destruction");
            Require(DestroyWindow(closing.value) != FALSE,
                    "destroy HWND while device is still referenced");
            closing.value = nullptr;
            Require(DisplayHooksReport().tracked_devices == 1,
                    "closed HWND retains COM lifetime tombstone");
            Require(UninstallDisplayHooks() == HRESULT_FROM_WIN32(ERROR_BUSY),
                    "closed HWND does not allow detaching from a live device");
            Require(retainedDevice->Present(nullptr, nullptr, nullptr,
                                            nullptr) == D3DERR_DEVICELOST,
                    "closed HWND produces explicit device-lost presentation");
            const ULONG finalClosedCount = retainedDevice.Detach()->Release();
            Require(finalClosedCount == 0 &&
                        DisplayHooksReport().tracked_devices == 0,
                    "closed-window device dies without touching a reused HWND");
        }
        {
            Window reentrant;
            auto parameters3 = parameters;
            parameters3.hDeviceWindow = reentrant.value;
            ComPtr<IDirect3DDevice9> reentrantDevice;
            Check(factory->CreateDevice(0, D3DDEVTYPE_HAL, reentrant.value,
                                        D3DCREATE_HARDWARE_VERTEXPROCESSING |
                                            D3DCREATE_FPU_PRESERVE,
                                        &parameters3, &reentrantDevice),
                  "third device for reentrant HWND destruction");
            destroyOnPositionChange = reentrant.value;
            Require(FAILED(SetDisplayDeviceMode9(reentrantDevice.Get(),
                                                 WindowMode::windowed)),
                    "reentrant window closure returns an explicit failure");
            Require(destroyOnPositionChange == nullptr &&
                        !IsWindow(reentrant.value),
                    "fixture destroyed HWND inside the backend SetWindowPos");
            reentrant.value = nullptr;
            Require(DisplayHooksReport().tracked_devices == 1,
                    "in-flight teardown preserves COM lifetime tracking");
            const ULONG count = reentrantDevice.Detach()->Release();
            Require(count == 0 && DisplayHooksReport().tracked_devices == 0,
                    "reentrant operation releases deferred GPU state safely");
        }
        factory.Reset();
        Check(UninstallDisplayHooks(),
              "public detours cleanly detached after all releases");
        Require(!DisplayHooksReport().installed, "detached state recorded");
        Require(logCalls < 40, "callback logging bounded to lifecycle/errors");
        std::cout << "display_hook_witness checks=" << checks
                  << " failures=0 logs=" << logCalls << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "display_hook_witness checks=" << checks
                  << " failed: " << error.what() << "\n";
        return 1;
    }
}
