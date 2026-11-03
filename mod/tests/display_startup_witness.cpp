// Source-owned fixture. No commercial executable or resource is loaded.
#include <d3d9.h>
#include <d3d9on12.h>
#include <windows.h>
#include <wrl/client.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using Microsoft::WRL::ComPtr;

// A real EXE initializer, after DLL attach/loader initialization and before
// main, deliberately calls the public factory without our helper API.
IDirect3D9* earlyFactory = Direct3DCreate9(D3D_SDK_VERSION);

void Check(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        std::cerr << operation << " HRESULT=0x" << std::hex
                  << static_cast<unsigned long>(result) << std::dec << '\n';
        throw std::runtime_error(operation);
    }
}

struct Window {
    HWND handle = nullptr;
    ~Window() {
        if (handle)
            DestroyWindow(handle);
    }
};
} // namespace

int main() {
    try {
        ComPtr<IDirect3D9> factory;
        factory.Attach(earlyFactory);
        earlyFactory = nullptr;
        if (!factory)
            throw std::runtime_error("EXE-initializer factory unavailable");
        SetProcessDpiAwarenessContext(
            DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WNDCLASSW klass{};
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpszClassName = L"TDUR.StartupGate.Witness";
        klass.lpfnWndProc = DefWindowProcW;
        if (!RegisterClassW(&klass))
            throw std::runtime_error("RegisterClassW witness");
        Window window;
        window.handle = CreateWindowExW(
            0, klass.lpszClassName,
            L"TDU reconstruction - deferred factory fixture (no game)",
            WS_OVERLAPPEDWINDOW, 80, 100, 320, 200, nullptr, nullptr,
            klass.hInstance, nullptr);
        if (!window.handle)
            throw std::runtime_error("CreateWindowExW witness");
        const auto foreground = GetForegroundWindow();
        D3DPRESENT_PARAMETERS params{};
        params.BackBufferWidth = 64;
        params.BackBufferHeight = 32;
        params.BackBufferFormat = D3DFMT_A8R8G8B8;
        params.BackBufferCount = 1;
        params.Windowed = TRUE;
        params.hDeviceWindow = window.handle;
        params.SwapEffect = D3DSWAPEFFECT_DISCARD;
        params.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        ComPtr<IDirect3DDevice9> device;
        Check(factory->CreateDevice(0, D3DDEVTYPE_HAL, window.handle,
                                    D3DCREATE_HARDWARE_VERTEXPROCESSING |
                                        D3DCREATE_FPU_PRESERVE,
                                    &params, &device),
              "CreateDevice through injected DLL");
        ComPtr<IDirect3DDevice9On12> interop;
        Check(device.As(&interop), "real public On12 interface");
        ComPtr<ID3D12Device> native;
        Check(interop->GetD3D12Device(IID_PPV_ARGS(&native)),
              "real D3D12 device");
        D3DDEVICE_CREATION_PARAMETERS creation{};
        Check(device->GetCreationParameters(&creation), "creation parameters");
        if (creation.hFocusWindow != window.handle ||
            params.hDeviceWindow != window.handle || !params.Windowed)
            throw std::runtime_error("public focus/presentation HWND changed");
        ComPtr<IDirect3DSwapChain9> swap;
        Check(device->GetSwapChain(0, &swap), "primary swapchain");
        D3DPRESENT_PARAMETERS underlying{};
        Check(swap->GetPresentParameters(&underlying),
              "underlying render sink");
        if (underlying.hDeviceWindow == window.handle ||
            IsWindowVisible(underlying.hDeviceWindow))
            throw std::runtime_error("separate hidden render sink missing");
        for (unsigned frame = 0; frame < 3; ++frame) {
            Check(device->BeginScene(), "BeginScene witness");
            Check(device->Clear(0, nullptr, D3DCLEAR_TARGET,
                                D3DCOLOR_ARGB(255, 15, 29, 180), 1.0F, 0),
                  "Clear witness");
            Check(device->EndScene(), "EndScene witness");
            Check(device->Present(nullptr, nullptr, nullptr, nullptr),
                  "deferred-hook DXGI presentation");
        }
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        RECT rectangle{};
        GetWindowRect(window.handle, &rectangle);
        GetMonitorInfoW(
            MonitorFromWindow(window.handle, MONITOR_DEFAULTTONEAREST),
            &monitor);
        const bool borderless =
            (GetWindowLongPtrW(window.handle, GWL_STYLE) & WS_CAPTION) == 0 &&
            EqualRect(&rectangle, &monitor.rcMonitor);
        const bool unfocused = GetForegroundWindow() == foreground &&
                               GetForegroundWindow() != window.handle;
        if (!borderless || !unfocused || IsWindowVisible(window.handle))
            throw std::runtime_error(
                "hidden borderless fixture stole focus or changed mode");
        std::array<wchar_t, 32768> privateRoot{};
        const auto length = GetEnvironmentVariableW(
            L"TDUR_PRIVATE_DOCUMENTS", privateRoot.data(),
            static_cast<DWORD>(privateRoot.size()));
        if (!length || length >= privateRoot.size())
            throw std::runtime_error("private fixture root missing");
        std::ofstream report(std::filesystem::path(privateRoot.data()) /
                             "display-startup-proof.json");
        report << "{\"success\":true,\"exe_initializer_factory\":true,"
                  "\"on12\":true,\"native_d3d12\":true,\"presents\":3,"
                  "\"borderless\":true,\"hidden\":true,\"unfocused\":true}\n";
        if (!report)
            throw std::runtime_error("cannot write private startup proof");
        std::cout << "PASS injected display startup gate, real On12, 3 "
                     "presents, hidden/unfocused\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
