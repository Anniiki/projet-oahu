#include "tdur/mod/display_hooks.hpp"

#include <wrl/client.h>

#include <algorithm>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace tdur::mod;
using Microsoft::WRL::ComPtr;
unsigned checks = 0;
std::mutex log_mutex;
std::vector<std::pair<std::string, HRESULT>> events;

void Require(bool condition, const char* message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
void Check(HRESULT result, const char* message) {
    if (FAILED(result))
        std::cerr << message << " HRESULT=" << std::hex
                  << static_cast<unsigned long>(result) << std::dec << '\n';
    Require(SUCCEEDED(result), message);
}
void Log(const char* event, HRESULT result, void*) noexcept {
    try {
        std::lock_guard lock(log_mutex);
        events.emplace_back(event, result);
    } catch (...) {
    }
}
bool HasEvent(const std::string& prefix, const std::string& details,
              HRESULT result) {
    std::lock_guard lock(log_mutex);
    return std::ranges::any_of(events, [&](const auto& event) {
        return event.first.starts_with(prefix) &&
               event.first.find(details) != std::string::npos &&
               event.second == result;
    });
}

struct Window {
    HWND value{};
    Window() {
        WNDCLASSW klass{};
        klass.lpfnWndProc = DefWindowProcW;
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpszClassName = L"TDUR.ResourceMetadata.OwnedFixture";
        Require(RegisterClassW(&klass) != 0, "register owned hidden window");
        value = CreateWindowExW(0, klass.lpszClassName,
                                L"TDU owned resource fixture (no game)",
                                WS_OVERLAPPEDWINDOW, 50, 50, 160, 120, nullptr,
                                nullptr, klass.hInstance, nullptr);
        Require(value != nullptr, "create hidden metadata fixture window");
    }
    ~Window() {
        if (value)
            DestroyWindow(value);
    }
};
} // namespace

int main() {
    try {
        Window window;
        const auto foreground = GetForegroundWindow();
        DisplayHookOptions options;
        options.enabled = true;
        options.observe_resource_metadata = true;
        options.display.mode = WindowMode::windowed;
        options.display.cap = FrameCapMode::unlimited;
        options.log = Log;
        Check(InstallDisplayHooks(options), "install observed public methods");
        ComPtr<IDirect3D9> factory;
        factory.Attach(Direct3DCreate9(D3D_SDK_VERSION));
        Require(factory != nullptr, "create classic On12 factory");
        D3DPRESENT_PARAMETERS parameters{};
        parameters.BackBufferWidth = 64;
        parameters.BackBufferHeight = 64;
        parameters.BackBufferFormat = D3DFMT_A8R8G8B8;
        parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        parameters.Windowed = TRUE;
        parameters.hDeviceWindow = window.value;
        parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        ComPtr<IDirect3DDevice9> device;
        Check(factory->CreateDevice(0, D3DDEVTYPE_HAL, window.value,
                                    D3DCREATE_HARDWARE_VERTEXPROCESSING |
                                        D3DCREATE_FPU_PRESERVE,
                                    &parameters, &device),
              "create owned source device");
        Require(DisplayHooksReport().resource_creation_calls == 0,
                "presenter-owned setup leaked into resource observations");

        D3DDISPLAYMODE display{};
        Check(factory->GetAdapterDisplayMode(0, &display),
              "query display format");
        D3DFORMAT color = D3DFMT_A16B16G16R16F;
        if (FAILED(factory->CheckDeviceFormat(0, D3DDEVTYPE_HAL, display.Format,
                                              D3DUSAGE_RENDERTARGET,
                                              D3DRTYPE_TEXTURE, color)))
            color = D3DFMT_A8R8G8B8;
        const auto depth = D3DFMT_D24S8;
        D3DMULTISAMPLE_TYPE aa = D3DMULTISAMPLE_NONE;
        for (const auto candidate :
             {D3DMULTISAMPLE_4_SAMPLES, D3DMULTISAMPLE_2_SAMPLES}) {
            DWORD color_quality = 0;
            DWORD depth_quality = 0;
            if (SUCCEEDED(factory->CheckDeviceMultiSampleType(
                    0, D3DDEVTYPE_HAL, color, TRUE, candidate,
                    &color_quality)) &&
                SUCCEEDED(factory->CheckDeviceMultiSampleType(
                    0, D3DDEVTYPE_HAL, depth, TRUE, candidate,
                    &depth_quality)) &&
                color_quality != 0 && depth_quality != 0) {
                aa = candidate;
                break;
            }
        }
        Check(factory->CheckDepthStencilMatch(0, D3DDEVTYPE_HAL, display.Format,
                                              color, depth),
              "native color/depth match");
        ComPtr<IDirect3DTexture9> texture;
        ComPtr<IDirect3DSurface9> target;
        ComPtr<IDirect3DSurface9> depth_surface;
        Check(device->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET, color,
                                    D3DPOOL_DEFAULT, &texture, nullptr),
              "create observed render texture");
        Check(device->CreateRenderTarget(64, 64, color, aa, 0, FALSE, &target,
                                         nullptr),
              "create observed RT");
        Check(device->CreateDepthStencilSurface(64, 64, depth, aa, 0, TRUE,
                                                &depth_surface, nullptr),
              "create observed depth");
        D3DSURFACE_DESC described{};
        Check(texture->GetLevelDesc(0, &described), "describe source texture");
        Require(described.Format == color &&
                    described.MultiSampleType == D3DMULTISAMPLE_NONE &&
                    described.Width == 64 && described.Height == 64,
                "texture descriptor changed by observation");
        Check(target->GetDesc(&described), "describe source RT");
        Require(described.Format == color && described.MultiSampleType == aa,
                "RT descriptor differs from native request");
        Check(depth_surface->GetDesc(&described), "describe source depth");
        Require(described.Format == depth && described.MultiSampleType == aa,
                "depth descriptor differs from native request");
        const auto details = std::string("actual=64x64 actual_format=") +
                             std::to_string(static_cast<unsigned>(color));
        Require(
            HasEvent("display-resource-created kind=texture", details, S_OK) &&
                HasEvent("display-resource-created kind=render_target",
                         details + " actual_msaa=" +
                             std::to_string(static_cast<unsigned>(aa)),
                         S_OK) &&
                HasEvent("display-resource-created kind=depth_stencil",
                         "actual_format=75", S_OK),
            "actual texture/RT/depth descriptions were not recorded");
        Require(DisplayHooksReport().resource_creation_calls == 3 &&
                    DisplayHooksReport().resource_creation_successes == 3,
                "resource API calls were missed or double-counted");
        for (unsigned i = 0; i < 3; ++i) {
            ComPtr<IDirect3DSurface9> duplicate;
            Check(device->CreateRenderTarget(64, 64, color, aa, 0, FALSE,
                                             &duplicate, nullptr),
                  "repeated native request remains unchanged");
        }
        {
            std::lock_guard lock(log_mutex);
            Require(
                std::ranges::count_if(events,
                                      [](const auto& event) {
                                          return event.first.starts_with(
                                              "display-resource-created ");
                                      }) == 3 &&
                    DisplayHooksReport().resource_creation_calls == 6 &&
                    DisplayHooksReport().resource_creation_successes == 6,
                "equal descriptors were logged repeatedly or lost from counts");
        }
        ComPtr<IDirect3DTexture9> managed;
        Check(device->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8,
                                    D3DPOOL_MANAGED, &managed, nullptr),
              "unobserved managed texture still works");
        Require(DisplayHooksReport().resource_creation_calls == 6,
                "ordinary asset texture was observed outside the RT scope");
        ComPtr<IDirect3DSurface9> invalid;
        const auto failure = device->CreateRenderTarget(
            0, 64, color, aa, 0, FALSE, &invalid, nullptr);
        Require(FAILED(failure) && !invalid &&
                    HasEvent("display-resource-created kind=render_target",
                             "actual=0x0", failure),
                "creation failure or absent descriptor was misreported");

        const auto before_present =
            DisplayHooksReport().resource_creation_calls;
        Check(device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff123456, 1, 0),
              "clear own fixture target without reading pixels");
        Check(device->Present(nullptr, nullptr, nullptr, nullptr),
              "present own fixture without reading pixels");
        Require(DisplayHooksReport().resource_creation_calls == before_present,
                "presenter-internal resource creation was not excluded");
        for (unsigned i = 0; i < 70; ++i) {
            ComPtr<IDirect3DTexture9> temporary;
            Check(device->CreateTexture(4 + i, 4, 1, D3DUSAGE_RENDERTARGET,
                                        D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                        &temporary, nullptr),
                  "bounded repeated RT metadata");
        }
        unsigned descriptions = 0;
        unsigned limits = 0;
        {
            std::lock_guard lock(log_mutex);
            for (const auto& [event, result] : events) {
                descriptions += event.starts_with("display-resource-created ");
                limits += event.starts_with("display-resource-metadata-limit ");
            }
        }
        Require(descriptions == 64 && limits == 1 &&
                    DisplayHooksReport().resource_creation_calls == 77 &&
                    DisplayHooksReport().resource_creation_successes == 76,
                "bounded log changed native creation results or counts");
        Require(GetForegroundWindow() == foreground &&
                    !IsWindowVisible(window.value),
                "metadata fixture changed focus or became visible");
        managed.Reset();
        texture.Reset();
        target.Reset();
        depth_surface.Reset();
        device.Reset();
        factory.Reset();
        Check(UninstallDisplayHooks(), "detach after all owned references");
        std::cout << "resource metadata: " << checks
                  << " checks passed; color=" << static_cast<unsigned>(color)
                  << " msaa=" << static_cast<unsigned>(aa)
                  << "; no pixel read\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
