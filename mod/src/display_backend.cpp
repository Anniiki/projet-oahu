#include "tdur/mod/display_backend.hpp"

#include <d3d9on12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace tdur::mod {
namespace {
using Microsoft::WRL::ComPtr;

void Check(HRESULT result, const char* operation) {
    if (FAILED(result))
        throw DisplayError(result, operation);
}

struct Handle {
    HANDLE value = nullptr;
    ~Handle() {
        if (value)
            CloseHandle(value);
    }
};

struct SystemD3D9 {
    HMODULE value = nullptr;
    ~SystemD3D9() {
        if (value)
            FreeLibrary(value);
    }
};

struct SinkWindow {
    HWND value = nullptr;
    ~SinkWindow() {
        if (value)
            DestroyWindow(value);
    }
};

std::string ErrorText(HRESULT result, const char* operation) {
    char text[256]{};
    std::snprintf(text, sizeof(text), "%s: HRESULT 0x%08lX", operation,
                  static_cast<unsigned long>(result));
    return text;
}

MonitorState DescribeMonitor(HMONITOR monitor) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info))
        throw DisplayError(HRESULT_FROM_WIN32(GetLastError()),
                           "GetMonitorInfoW");
    MonitorState result;
    result.handle = monitor;
    result.bounds = info.rcMonitor;
    result.work_area = info.rcWork;
    result.gdi_name = info.szDevice;
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsExW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode,
                               0) &&
        mode.dmDisplayFrequency > 1) {
        result.refresh_numerator = mode.dmDisplayFrequency;
    }

    // The integer GDI rate rounds e.g. 60000/1001. Match the current source to
    // its active DisplayConfig paths, retaining the exact rational rate.
    for (unsigned retry = 0; retry < 3; ++retry) {
        UINT32 pathCount = 0;
        UINT32 modeCount = 0;
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount,
                                        &modeCount) != ERROR_SUCCESS)
            break;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        const LONG status =
            QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(),
                               &modeCount, modes.data(), nullptr);
        if (status == ERROR_INSUFFICIENT_BUFFER)
            continue;
        if (status != ERROR_SUCCESS)
            break;
        for (UINT32 index = 0; index < pathCount; ++index) {
            const auto& path = paths[index];
            DISPLAYCONFIG_SOURCE_DEVICE_NAME name{};
            name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            name.header.size = sizeof(name);
            name.header.adapterId = path.sourceInfo.adapterId;
            name.header.id = path.sourceInfo.id;
            if (DisplayConfigGetDeviceInfo(&name.header) != ERROR_SUCCESS ||
                result.gdi_name != name.viewGdiDeviceName)
                continue;
            const auto rate = path.targetInfo.refreshRate;
            if (rate.Numerator == 0 || rate.Denominator == 0)
                continue;
            const double hz =
                static_cast<double>(rate.Numerator) / rate.Denominator;
            if (result.rational_refresh_observed) {
                const bool differs =
                    static_cast<uint64_t>(result.refresh_numerator) *
                        rate.Denominator !=
                    static_cast<uint64_t>(rate.Numerator) *
                        result.refresh_denominator;
                result.cloned_source_ambiguous |= differs;
                if (hz >= result.refresh_hz())
                    continue;
            }
            result.refresh_numerator = rate.Numerator;
            result.refresh_denominator = rate.Denominator;
            result.rational_refresh_observed = true;
        }
        break;
    }
    return result;
}

BOOL CALLBACK CollectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM parameter) {
    auto& list = *reinterpret_cast<std::vector<HMONITOR>*>(parameter);
    list.push_back(monitor);
    return TRUE;
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* resource,
                                  D3D12_RESOURCE_STATES before,
                                  D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    return barrier;
}

constexpr char BlitShader[] = R"(
Texture2D<float4> sourceTexture : register(t0);
SamplerState pointSampler : register(s0);
struct Vertex { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Vertex vsMain(uint id : SV_VertexID) {
    Vertex v;
    v.uv = float2((id << 1) & 2, id & 2);
    v.position = float4(v.uv.x * 2 - 1, 1 - v.uv.y * 2, 0, 1);
    return v;
}
float4 psMain(Vertex v) : SV_Target {
    return float4(sourceTexture.SampleLevel(pointSampler, v.uv, 0).rgb, 1);
}
)";

DXGI_FORMAT SampleFormat(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_TYPELESS:
    case DXGI_FORMAT_B8G8R8X8_UNORM:
        return DXGI_FORMAT_B8G8R8X8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    default:
        throw DisplayError(DXGI_ERROR_UNSUPPORTED,
                           "unsupported D3D9On12 source format");
    }
}
HMODULE ResidentSystemD3D9() {
    static const HMODULE module = [] {
        std::array<wchar_t, MAX_PATH> path{};
        const UINT length =
            GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size()));
        if (length == 0 || length + 11 >= path.size())
            throw DisplayError(E_FAIL, "GetSystemDirectoryW d3d9");
        const std::wstring file =
            std::wstring(path.data(), length) + L"\\d3d9.dll";
        const HMODULE loaded =
            LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!loaded)
            throw DisplayError(HRESULT_FROM_WIN32(GetLastError()),
                               "LoadLibraryExW system d3d9");
        return loaded;
    }();
    // No static destructor calls FreeLibrary during loader teardown. Public
    // COM methods remain backed by their real system module until process exit.
    return module;
}
} // namespace

IDirect3D9* CreateFactory9On12(UINT sdkVersion) {
    const FARPROC address =
        GetProcAddress(ResidentSystemD3D9(), "Direct3DCreate9On12");
    if (!address)
        throw DisplayError(DXGI_ERROR_UNSUPPORTED,
                           "Direct3DCreate9On12 missing");
    PFN_Direct3DCreate9On12 create = nullptr;
    static_assert(sizeof(create) == sizeof(address));
    std::memcpy(&create, &address, sizeof(create));
    D3D9ON12_ARGS overrides{};
    overrides.Enable9On12 = TRUE;
    auto* result = create(sdkVersion, &overrides, 1);
    if (!result)
        throw DisplayError(D3DERR_NOTAVAILABLE, "Direct3DCreate9On12");
    return result;
}

DisplayError::DisplayError(HRESULT result, const char* operation)
    : std::runtime_error(ErrorText(result, operation)), result_(result) {}

double MonitorState::refresh_hz() const noexcept {
    return refresh_denominator == 0
               ? 0
               : static_cast<double>(refresh_numerator) / refresh_denominator;
}

double EffectiveFrameCap(const DisplayPreferences& preferences,
                         double refresh) {
    if (preferences.cap == FrameCapMode::unlimited)
        return 0;
    if (preferences.cap == FrameCapMode::fixed) {
        if (!std::isfinite(preferences.fixed_fps) ||
            preferences.fixed_fps < 1 || preferences.fixed_fps > 1000)
            throw std::invalid_argument("fixed frame cap must be in [1,1000]");
        return preferences.fixed_fps;
    }
    if (!std::isfinite(refresh) || refresh < 1 || refresh > 1000)
        throw std::invalid_argument("invalid monitor refresh rate");
    return preferences.cap == FrameCapMode::native_minus_three
               ? std::max(1.0, refresh - 3.0)
               : refresh;
}

PresentationPolicy SelectPresentationPolicy(bool exclusive, bool tearing,
                                            bool allowTearing,
                                            bool vsync) noexcept {
    PresentationPolicy policy;
    policy.sync_interval = vsync ? 1U : 0U;
    policy.vrr_eligible = !exclusive && tearing && allowTearing && !vsync;
    policy.present_flags =
        policy.vrr_eligible ? DXGI_PRESENT_ALLOW_TEARING : 0U;
    return policy;
}

struct DisplayBackend::Impl {
    // Kept alive until all D3D9 COM objects have been destroyed.
    SystemD3D9 runtime;
    SinkWindow sink;
    HWND window;
    DWORD ownerThread;
    LONG_PTR originalStyle;
    LONG_PTR originalExStyle;
    WINDOWPLACEMENT originalPlacement{sizeof(WINDOWPLACEMENT)};
    DisplayPreferences preferences;
    DisplayState status;
    LARGE_INTEGER frequency{};
    int64_t nextDeadline = 0;
    ULONGLONG lastMonitorQuery = 0;
    Handle pacingTimer;
    Handle fenceEvent;
    ComPtr<IDirect3D9Ex> factory9;
    ComPtr<IDirect3D9> compatibilityFactory9;
    IDirect3DDevice9* device9 = nullptr;
    ComPtr<IDirect3DDevice9Ex> reference9;
    ComPtr<IDirect3DSurface9> referenceTarget;
    ComPtr<ID3D12Device> device;
    ComPtr<IDXGIFactory5> factory;
    ComPtr<IDXGISwapChain3> swapchain;
    std::array<ComPtr<ID3D12Resource>, 2> buffers;
    UINT swapWidth = 0;
    UINT swapHeight = 0;
    UINT swapFlags = 0;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12Fence> fence;
    uint64_t fenceValue = 0;
    ComPtr<ID3D12DescriptorHeap> rtvs;
    ComPtr<ID3D12DescriptorHeap> srvs;
    UINT rtvIncrement = 0;
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    struct BlitLifetime {
        ComPtr<IDirect3DSurface9> source;
        ComPtr<IDirect3DDevice9On12> interop;
        ComPtr<ID3D12Resource> unwrapped;
        ComPtr<ID3D12Resource> sampled;
        ComPtr<ID3D12Resource> output;
        ComPtr<ID3D12Resource> readback;
        uint64_t fence_value = 0;
        bool signaled = false;
        bool returned = false;
    };
    std::unique_ptr<BlitLifetime> pendingBlit;

    Impl(HWND target, DisplayPreferences prefs)
        : window(target), ownerThread(GetCurrentThreadId()),
          originalStyle(GetWindowLongPtrW(target, GWL_STYLE)),
          originalExStyle(GetWindowLongPtrW(target, GWL_EXSTYLE)),
          preferences(prefs) {
        if (!IsWindow(window) ||
            GetWindowThreadProcessId(window, nullptr) != ownerThread)
            throw std::invalid_argument(
                "display HWND must belong to this thread");
        if (!GetWindowPlacement(window, &originalPlacement))
            throw DisplayError(HRESULT_FROM_WIN32(GetLastError()),
                               "GetWindowPlacement");
        QueryPerformanceFrequency(&frequency);
        pacingTimer.value = CreateWaitableTimerExW(
            nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!pacingTimer.value)
            pacingTimer.value = CreateWaitableTimerW(nullptr, FALSE, nullptr);
        if (!pacingTimer.value)
            throw DisplayError(HRESULT_FROM_WIN32(GetLastError()),
                               "CreateWaitableTimer");
        Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)),
              "CreateDXGIFactory2");
        BOOL allow = FALSE;
        status.tearing_supported =
            SUCCEEDED(factory->CheckFeatureSupport(
                DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))) &&
            allow;
        swapFlags =
            status.tearing_supported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0U;
        WNDCLASSW klass{};
        klass.lpfnWndProc = DefWindowProcW;
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpszClassName = L"TDUR.D3D9On12.RenderSink.v1";
        if (!RegisterClassW(&klass) &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw DisplayError(HRESULT_FROM_WIN32(GetLastError()),
                               "RegisterClassW render sink");
        sink.value = CreateWindowExW(
            0, klass.lpszClassName, L"TDU reconstruction D3D9On12 render sink",
            WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, klass.hInstance, nullptr);
        if (!sink.value)
            throw DisplayError(HRESULT_FROM_WIN32(GetLastError()),
                               "CreateWindowExW render sink");
        refresh();
    }

    void restoreWindow() noexcept {
        if (swapchain)
            swapchain->SetFullscreenState(FALSE, nullptr);
        if (IsWindow(window) && GetCurrentThreadId() == ownerThread) {
            SetWindowLongPtrW(window, GWL_STYLE, originalStyle);
            SetWindowLongPtrW(window, GWL_EXSTYLE, originalExStyle);
            auto placement = originalPlacement;
            // GetWindowPlacement can report SW_SHOWNORMAL for a window that
            // has never been shown. Do not turn destruction into ShowWindow.
            if ((originalStyle & WS_VISIBLE) == 0)
                placement.showCmd = SW_HIDE;
            else if (placement.showCmd == SW_SHOWNORMAL)
                placement.showCmd = SW_SHOWNOACTIVATE;
            SetWindowPlacement(window, &placement);
            SetWindowPos(window, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                             SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    }

    ~Impl() { restoreWindow(); }

    void checkUsable() {
        if (FAILED(status.backend_failure))
            throw DisplayError(
                status.backend_failure,
                "display backend is in a persistent failure state");
        if (device) {
            const auto reason = device->GetDeviceRemovedReason();
            if (FAILED(reason)) {
                status.backend_failure = reason;
                throw DisplayError(reason, "D3D12 device availability");
            }
        }
    }

    void owner() const {
        if (GetCurrentThreadId() != ownerThread)
            throw std::logic_error(
                "display operation requires the HWND thread");
    }

    void refresh() {
        owner();
        const auto monitor =
            MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
        const auto now = GetTickCount64();
        if (monitor != status.monitor.handle || now - lastMonitorQuery >= 500) {
            const auto previous = status.monitor;
            status.monitor = DescribeMonitor(monitor);
            lastMonitorQuery = now;
            if (previous.handle != monitor ||
                previous.refresh_numerator !=
                    status.monitor.refresh_numerator ||
                previous.refresh_denominator !=
                    status.monitor.refresh_denominator)
                nextDeadline = 0;
        }
        status.effective_cap_hz =
            EffectiveFrameCap(preferences, status.monitor.refresh_hz());
        const HWND foreground = GetForegroundWindow();
        DWORD foregroundProcess = 0;
        if (foreground)
            GetWindowThreadProcessId(foreground, &foregroundProcess);
        status.window_has_foreground = foreground == window;
        status.process_has_foreground =
            foregroundProcess == GetCurrentProcessId();
        BOOL exclusive = FALSE;
        if (swapchain)
            Check(swapchain->GetFullscreenState(&exclusive, nullptr),
                  "GetFullscreenState");
        status.exclusive_active = exclusive != FALSE;
        status.requested_mode = preferences.mode;
        status.exclusive_transition_pending =
            preferences.mode == WindowMode::exclusive &&
            !status.exclusive_active;
    }

    void style(WindowMode mode) {
        owner();
        refresh();
        const LONG_PTR visible =
            GetWindowLongPtrW(window, GWL_STYLE) & WS_VISIBLE;
        RECT rectangle{};
        LONG_PTR styleBits = WS_CLIPCHILDREN | WS_CLIPSIBLINGS | visible;
        if (mode == WindowMode::windowed) {
            styleBits |= WS_OVERLAPPEDWINDOW;
            const RECT work = status.monitor.work_area;
            const LONG width = static_cast<LONG>(std::min<uint32_t>(
                preferences.window_width,
                static_cast<uint32_t>(work.right - work.left)));
            const LONG height = static_cast<LONG>(std::min<uint32_t>(
                preferences.window_height,
                static_cast<uint32_t>(work.bottom - work.top)));
            rectangle = {0, 0, std::max(1L, width), std::max(1L, height)};
            if (!AdjustWindowRectExForDpi(&rectangle,
                                          static_cast<DWORD>(styleBits), FALSE,
                                          0, GetDpiForWindow(window)))
                throw DisplayError(HRESULT_FROM_WIN32(GetLastError()),
                                   "AdjustWindowRectExForDpi");
            const LONG outerWidth = std::min(rectangle.right - rectangle.left,
                                             work.right - work.left);
            const LONG outerHeight = std::min(rectangle.bottom - rectangle.top,
                                              work.bottom - work.top);
            rectangle.left =
                work.left + (work.right - work.left - outerWidth) / 2;
            rectangle.top =
                work.top + (work.bottom - work.top - outerHeight) / 2;
            rectangle.right = rectangle.left + outerWidth;
            rectangle.bottom = rectangle.top + outerHeight;
        } else {
            styleBits |= WS_POPUP;
            rectangle = status.monitor.bounds;
        }
        SetLastError(ERROR_SUCCESS);
        const LONG_PTR old = SetWindowLongPtrW(window, GWL_STYLE, styleBits);
        if (old == 0 && GetLastError() != ERROR_SUCCESS)
            throw DisplayError(HRESULT_FROM_WIN32(GetLastError()),
                               "SetWindowLongPtrW style");
        // No focus spoof, topmost window, desktop mode or system setting.
        const LONG_PTR exStyle =
            originalExStyle &
            ~(WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE |
              WS_EX_STATICEDGE | WS_EX_TOPMOST);
        SetWindowLongPtrW(window, GWL_EXSTYLE, exStyle);
        if (!SetWindowPos(window, nullptr, rectangle.left, rectangle.top,
                          rectangle.right - rectangle.left,
                          rectangle.bottom - rectangle.top,
                          SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED))
            throw DisplayError(HRESULT_FROM_WIN32(GetLastError()),
                               "SetWindowPos");
    }

    void maintainBorderless() {
        owner();
        if (preferences.mode != WindowMode::borderless ||
            status.exclusive_active || !IsWindow(window) || IsIconic(window))
            return;
        RECT actual{};
        if (!GetWindowRect(window, &actual))
            throw DisplayError(HRESULT_FROM_WIN32(GetLastError()),
                               "GetWindowRect borderless");
        const auto& target = status.monitor.bounds;
        const auto bits = GetWindowLongPtrW(window, GWL_STYLE);
        const auto edges =
            GetWindowLongPtrW(window, GWL_EXSTYLE) &
            (WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE |
             WS_EX_STATICEDGE | WS_EX_TOPMOST);
        if ((bits & (WS_POPUP | WS_CAPTION | WS_THICKFRAME)) != WS_POPUP ||
            edges || actual.left != target.left || actual.top != target.top ||
            actual.right != target.right || actual.bottom != target.bottom)
            // The host can resize its visible HWND after CreateDevice/Reset.
            // Repair only a mismatch; stable frames never call SetWindowPos.
            style(WindowMode::borderless);
    }

    void loadRuntime() {
        owner();
        if (runtime.value)
            return;
        std::array<wchar_t, MAX_PATH> path{};
        const UINT length =
            GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size()));
        if (length == 0 || length + 11 >= path.size())
            throw DisplayError(E_FAIL, "GetSystemDirectoryW d3d9");
        const std::wstring file =
            std::wstring(path.data(), length) + L"\\d3d9.dll";
        runtime.value =
            LoadLibraryExW(file.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!runtime.value)
            throw DisplayError(HRESULT_FROM_WIN32(GetLastError()),
                               "LoadLibraryExW system d3d9");
    }

    IDirect3D9* factory9on12Compat() {
        owner();
        if (compatibilityFactory9)
            return compatibilityFactory9.Get();
        compatibilityFactory9.Attach(CreateFactory9On12());
        return compatibilityFactory9.Get();
    }

    IDirect3D9Ex* factory9on12() {
        loadRuntime();
        if (factory9)
            return factory9.Get();
        const FARPROC address =
            GetProcAddress(runtime.value, "Direct3DCreate9On12Ex");
        if (!address)
            throw DisplayError(DXGI_ERROR_UNSUPPORTED,
                               "Direct3DCreate9On12Ex missing");
        PFN_Direct3DCreate9On12Ex create = nullptr;
        static_assert(sizeof(create) == sizeof(address));
        std::memcpy(&create, &address, sizeof(create));
        D3D9ON12_ARGS overrides{};
        overrides.Enable9On12 = TRUE;
        Check(create(D3D_SDK_VERSION, &overrides, 1, &factory9),
              "Direct3DCreate9On12Ex");
        return factory9.Get();
    }

    void initializeDevice(IDirect3DDevice9* candidate) {
        owner();
        if (!candidate || device9)
            throw std::invalid_argument(
                "attach exactly one nonnull D3D9 device");
        ComPtr<IDirect3DDevice9On12> checked;
        Check(candidate->QueryInterface(IID_PPV_ARGS(&checked)),
              "QueryInterface IDirect3DDevice9On12");
        Check(checked->GetD3D12Device(IID_PPV_ARGS(&device)),
              "GetD3D12Device (no classic D3D9 fallback)");
        device9 = candidate;
        D3DCAPS9 caps{};
        Check(device9->GetDeviceCaps(&caps), "GetDeviceCaps");
        status.maximum_anisotropy = caps.MaxAnisotropy;
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        Check(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)),
              "CreateCommandQueue");
        Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&allocator)),
              "CreateCommandAllocator");
        Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        allocator.Get(), nullptr,
                                        IID_PPV_ARGS(&commands)),
              "CreateCommandList");
        Check(commands->Close(), "initial Close");
        Check(
            device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
            "CreateFence");
        fenceEvent.value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!fenceEvent.value)
            throw DisplayError(HRESULT_FROM_WIN32(GetLastError()),
                               "CreateEventW");
        D3D12_DESCRIPTOR_HEAP_DESC heap{};
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heap.NumDescriptors = 3;
        Check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtvs)),
              "CreateDescriptorHeap RTV");
        rtvIncrement = device->GetDescriptorHandleIncrementSize(heap.Type);
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heap.NumDescriptors = 1;
        heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Check(device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&srvs)),
              "CreateDescriptorHeap SRV");
        makePipeline();
        status.d3d9on12_verified = true;
        refresh();
        enterRequestedExclusiveIfReady();
    }

    void makePipeline() {
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        D3D12_ROOT_PARAMETER parameter{};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameter.DescriptorTable = {1, &range};
        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW =
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC signature{};
        signature.NumParameters = 1;
        signature.pParameters = &parameter;
        signature.NumStaticSamplers = 1;
        signature.pStaticSamplers = &sampler;
        ComPtr<ID3DBlob> encoded;
        ComPtr<ID3DBlob> diagnostics;
        Check(D3D12SerializeRootSignature(&signature,
                                          D3D_ROOT_SIGNATURE_VERSION_1,
                                          &encoded, &diagnostics),
              "D3D12SerializeRootSignature");
        Check(device->CreateRootSignature(0, encoded->GetBufferPointer(),
                                          encoded->GetBufferSize(),
                                          IID_PPV_ARGS(&root)),
              "CreateRootSignature");
        ComPtr<ID3DBlob> vertex;
        ComPtr<ID3DBlob> pixel;
        constexpr UINT flags =
            D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS;
        Check(D3DCompile(BlitShader, sizeof(BlitShader) - 1,
                         "tdur-display-blit", nullptr, nullptr, "vsMain",
                         "vs_5_0", flags, 0, &vertex, &diagnostics),
              "D3DCompile vertex");
        Check(D3DCompile(BlitShader, sizeof(BlitShader) - 1,
                         "tdur-display-blit", nullptr, nullptr, "psMain",
                         "ps_5_0", flags, 0, &pixel, &diagnostics),
              "D3DCompile pixel");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = root.Get();
        pso.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
        pso.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
        pso.BlendState.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso.RasterizerState.DepthClipEnable = TRUE;
        pso.SampleMask = UINT_MAX;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
        pso.SampleDesc.Count = 1;
        Check(
            device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&pipeline)),
            "CreateGraphicsPipelineState");
    }

    ComPtr<IDXGIOutput> currentOutput() {
        const LUID luid = device->GetAdapterLuid();
        ComPtr<IDXGIAdapter1> adapter;
        Check(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)),
              "EnumAdapterByLuid");
        for (UINT index = 0;; ++index) {
            ComPtr<IDXGIOutput> output;
            const HRESULT result = adapter->EnumOutputs(index, &output);
            if (result == DXGI_ERROR_NOT_FOUND)
                break;
            Check(result, "EnumOutputs");
            DXGI_OUTPUT_DESC desc{};
            Check(output->GetDesc(&desc), "GetDesc output");
            if (desc.Monitor == status.monitor.handle)
                return output;
        }
        throw DisplayError(DXGI_ERROR_NOT_FOUND,
                           "monitor is not an output of the rendering adapter");
    }

    void ensureSwapchain() {
        if (!queue)
            throw DisplayError(E_UNEXPECTED,
                               "attach D3D9On12 before presentation");
        RECT client{};
        GetClientRect(window, &client);
        const UINT width = static_cast<UINT>(std::max(1L, client.right));
        const UINT height = static_cast<UINT>(std::max(1L, client.bottom));
        if (!swapchain) {
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width = width;
            desc.Height = height;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.Flags = swapFlags;
            ComPtr<IDXGISwapChain1> created;
            Check(factory->CreateSwapChainForHwnd(queue.Get(), window, &desc,
                                                  nullptr, nullptr, &created),
                  "CreateSwapChainForHwnd D3D12");
            Check(created.As(&swapchain), "QueryInterface IDXGISwapChain3");
            Check(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER),
                  "MakeWindowAssociation");
        } else if (width != swapWidth || height != swapHeight) {
            for (auto& buffer : buffers)
                buffer.Reset();
            Check(swapchain->ResizeBuffers(
                      2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, swapFlags),
                  "ResizeBuffers");
        }
        swapWidth = width;
        swapHeight = height;
        auto handle = rtvs->GetCPUDescriptorHandleForHeapStart();
        for (UINT index = 0; index < buffers.size(); ++index) {
            if (!buffers[index])
                Check(
                    swapchain->GetBuffer(index, IID_PPV_ARGS(&buffers[index])),
                    "GetBuffer");
            device->CreateRenderTargetView(buffers[index].Get(), nullptr,
                                           handle);
            handle.ptr += rtvIncrement;
        }
    }

    void setMode(WindowMode mode) {
        owner();
        checkUsable();
        if (mode != WindowMode::windowed && mode != WindowMode::borderless &&
            mode != WindowMode::exclusive)
            throw std::invalid_argument("unknown window mode");
        if (mode == WindowMode::exclusive && !queue)
            throw DisplayError(E_UNEXPECTED,
                               "exclusive mode needs an attached device");
        if (mode == WindowMode::exclusive &&
            (!IsWindowVisible(window) || GetForegroundWindow() != window))
            throw DisplayError(DXGI_ERROR_NOT_CURRENTLY_AVAILABLE,
                               "exclusive requires a visible foreground HWND");
        const auto oldMode = preferences.mode;
        const LONG_PTR oldStyle = GetWindowLongPtrW(window, GWL_STYLE);
        const LONG_PTR oldExStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        RECT oldRectangle{};
        GetWindowRect(window, &oldRectangle);
        BOOL wasExclusive = FALSE;
        ComPtr<IDXGIOutput> oldOutput;
        if (swapchain)
            Check(swapchain->GetFullscreenState(&wasExclusive, &oldOutput),
                  "save fullscreen state");
        try {
            if (mode != WindowMode::exclusive && swapchain) {
                Check(swapchain->SetFullscreenState(FALSE, nullptr),
                      "SetFullscreenState windowed");
            }
            style(mode);
            if (mode == WindowMode::exclusive) {
                ensureSwapchain();
                const auto output = currentOutput();
                Check(swapchain->SetFullscreenState(TRUE, output.Get()),
                      "SetFullscreenState exclusive (no window-style "
                      "substitute)");
                BOOL active = FALSE;
                Check(swapchain->GetFullscreenState(&active, nullptr),
                      "verify exclusive state");
                if (!active)
                    throw DisplayError(DXGI_ERROR_NOT_CURRENTLY_AVAILABLE,
                                       "exclusive state is not active");
                // A flip swapchain must resize buffers after entering
                // exclusive.
                for (auto& buffer : buffers)
                    buffer.Reset();
                Check(swapchain->ResizeBuffers(2, swapWidth, swapHeight,
                                               DXGI_FORMAT_B8G8R8A8_UNORM,
                                               swapFlags),
                      "ResizeBuffers exclusive");
            }
            preferences.mode = mode;
            nextDeadline = 0;
            refresh();
        } catch (...) {
            // A refused exclusive transition must not leave a borderless
            // window labeled windowed. Restore the previous public API state.
            if (swapchain)
                swapchain->SetFullscreenState(wasExclusive, oldOutput.Get());
            SetWindowLongPtrW(window, GWL_STYLE, oldStyle);
            SetWindowLongPtrW(window, GWL_EXSTYLE, oldExStyle);
            SetWindowPos(window, nullptr, oldRectangle.left, oldRectangle.top,
                         oldRectangle.right - oldRectangle.left,
                         oldRectangle.bottom - oldRectangle.top,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            preferences.mode = oldMode;
            nextDeadline = 0;
            throw;
        }
    }

    void enterRequestedExclusiveIfReady() {
        // The retail HWND is often still hidden during CreateDevice. Keep
        // its requested mode pending without fabricating visibility/focus or
        // claiming exclusive ownership. Transition only when it is eligible.
        if (status.exclusive_transition_pending && queue &&
            status.window_has_foreground && IsWindowVisible(window))
            setMode(WindowMode::exclusive);
    }

    ComPtr<IDirect3DSurface9> sourceSurface(IDirect3DSurface9* surface) {
        if (!surface || !device9)
            throw std::invalid_argument(
                "a surface and attached device are required");
        ComPtr<IDirect3DDevice9> sourceDevice;
        Check(surface->GetDevice(&sourceDevice), "GetDevice surface");
        ComPtr<IUnknown> expectedIdentity;
        ComPtr<IUnknown> actualIdentity;
        Check(device9->QueryInterface(IID_PPV_ARGS(&expectedIdentity)),
              "device identity");
        Check(sourceDevice.As(&actualIdentity), "surface device identity");
        if (expectedIdentity.Get() != actualIdentity.Get())
            throw std::invalid_argument(
                "surface belongs to a different D3D9 device");
        D3DSURFACE_DESC desc{};
        Check(surface->GetDesc(&desc), "GetDesc D3D9 surface");
        if (desc.Format != D3DFMT_A8R8G8B8 && desc.Format != D3DFMT_X8R8G8B8)
            throw DisplayError(DXGI_ERROR_UNSUPPORTED,
                               "only 32-bit RGB source surfaces supported");
        ComPtr<IDirect3DSurface9> source = surface;
        if (desc.MultiSampleType != D3DMULTISAMPLE_NONE) {
            ComPtr<IDirect3DSurface9> resolved9;
            Check(device9->CreateRenderTarget(desc.Width, desc.Height,
                                              desc.Format, D3DMULTISAMPLE_NONE,
                                              0, FALSE, &resolved9, nullptr),
                  "CreateRenderTarget MSAA resolve");
            Check(device9->StretchRect(surface, nullptr, resolved9.Get(),
                                       nullptr, D3DTEXF_NONE),
                  "StretchRect MSAA resolve");
            source = resolved9;
        }
        return source;
    }

    void waitFence(uint64_t value) {
        const auto completed = fence->GetCompletedValue();
        if (completed == std::numeric_limits<uint64_t>::max())
            throw DisplayError(DXGI_ERROR_DEVICE_REMOVED,
                               "fence reports device removal");
        if (completed >= value)
            return;
        Check(fence->SetEventOnCompletion(value, fenceEvent.value),
              "SetEventOnCompletion");
        if (WaitForSingleObject(fenceEvent.value, 10000) != WAIT_OBJECT_0)
            throw DisplayError(DXGI_ERROR_DEVICE_HUNG, "D3D12 fence timeout");
        const auto after = fence->GetCompletedValue();
        if (after == std::numeric_limits<uint64_t>::max())
            throw DisplayError(DXGI_ERROR_DEVICE_REMOVED,
                               "device removed while awaiting the fence");
        if (after < value)
            throw DisplayError(E_UNEXPECTED, "fence event without completion");
    }

    // A checkout is always returned in COMMON. Commands are synchronous here:
    // no descriptor, allocator or D3D9 resource is reused before its own fence.
    void blit(IDirect3DSurface9* input, ID3D12Resource* output,
              D3D12_CPU_DESCRIPTOR_HANDLE rtv, UINT width, UINT height,
              D3D12_RESOURCE_STATES finalState,
              ID3D12Resource* readback = nullptr,
              const D3D12_PLACED_SUBRESOURCE_FOOTPRINT* footprint = nullptr,
              D3D12_RESOURCE_STATES initialState =
                  D3D12_RESOURCE_STATE_RENDER_TARGET) {
        checkUsable();
        if (pendingBlit)
            throw DisplayError(E_UNEXPECTED,
                               "a GPU submission is still retained");
        const auto source = sourceSurface(input);
        ComPtr<IDirect3DDevice9On12> interop;
        Check(device9->QueryInterface(IID_PPV_ARGS(&interop)),
              "QueryInterface interop for this operation");
        Check(allocator->Reset(), "Reset allocator");
        Check(commands->Reset(allocator.Get(), pipeline.Get()),
              "Reset commands");
        // Allocate ownership before checkout/submission, not while unwinding
        // a failed Signal. Every resource referenced by the GPU survives until
        // its own completion is proved, including local readback destinations.
        auto lifetime = std::make_unique<BlitLifetime>();
        lifetime->source = source;
        lifetime->interop = interop;
        lifetime->output = output;
        lifetime->readback = readback;
        ComPtr<ID3D12Resource> resource;
        Check(interop->UnwrapUnderlyingResource(source.Get(), queue.Get(),
                                                IID_PPV_ARGS(&resource)),
              "UnwrapUnderlyingResource");
        lifetime->unwrapped = resource;
        bool submitted = false;
        bool returned = false;
        try {
            const auto sourceDesc = resource->GetDesc();
            if (sourceDesc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
                sourceDesc.SampleDesc.Count != 1 || sourceDesc.MipLevels != 1 ||
                sourceDesc.DepthOrArraySize != 1)
                throw DisplayError(
                    DXGI_ERROR_UNSUPPORTED,
                    "unwrapped source is not a resolved 2D texture");
            // A standalone D3D9 render target may deny shader resources. Copy
            // it to our own sampleable texture instead of inventing an SRV on
            // a resource whose creation flags forbid that use.
            auto sampleDesc = sourceDesc;
            sampleDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
            sampleDesc.Alignment = 0;
            D3D12_HEAP_PROPERTIES sampleHeap{};
            sampleHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
            ComPtr<ID3D12Resource> sampled;
            Check(device->CreateCommittedResource(
                      &sampleHeap, D3D12_HEAP_FLAG_NONE, &sampleDesc,
                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                      IID_PPV_ARGS(&sampled)),
                  "Create sampleable interop copy");
            lifetime->sampled = sampled;
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = SampleFormat(sourceDesc.Format);
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Shader4ComponentMapping =
                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture2D.MipLevels = 1;
            device->CreateShaderResourceView(
                sampled.Get(), &srv,
                srvs->GetCPUDescriptorHandleForHeapStart());
            if (initialState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
                auto targetBarrier = Transition(
                    output, initialState, D3D12_RESOURCE_STATE_RENDER_TARGET);
                commands->ResourceBarrier(1, &targetBarrier);
            }
            auto barrier =
                Transition(resource.Get(), D3D12_RESOURCE_STATE_COMMON,
                           D3D12_RESOURCE_STATE_COPY_SOURCE);
            commands->ResourceBarrier(1, &barrier);
            commands->CopyResource(sampled.Get(), resource.Get());
            barrier =
                Transition(resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                           D3D12_RESOURCE_STATE_COMMON);
            commands->ResourceBarrier(1, &barrier);
            barrier = Transition(sampled.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            commands->ResourceBarrier(1, &barrier);
            commands->SetGraphicsRootSignature(root.Get());
            ID3D12DescriptorHeap* heaps[]{srvs.Get()};
            commands->SetDescriptorHeaps(1, heaps);
            commands->SetGraphicsRootDescriptorTable(
                0, srvs->GetGPUDescriptorHandleForHeapStart());
            const D3D12_VIEWPORT viewport{
                0, 0, static_cast<float>(width), static_cast<float>(height),
                0, 1};
            const D3D12_RECT scissor{0, 0, static_cast<LONG>(width),
                                     static_cast<LONG>(height)};
            commands->RSSetViewports(1, &viewport);
            commands->RSSetScissorRects(1, &scissor);
            commands->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            commands->IASetPrimitiveTopology(
                D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commands->DrawInstanced(3, 1, 0, 0);
            barrier = Transition(output, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                 finalState);
            commands->ResourceBarrier(1, &barrier);
            if (readback && footprint) {
                D3D12_TEXTURE_COPY_LOCATION destination{};
                destination.pResource = readback;
                destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                destination.PlacedFootprint = *footprint;
                D3D12_TEXTURE_COPY_LOCATION origin{};
                origin.pResource = output;
                origin.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                commands->CopyTextureRegion(&destination, 0, 0, 0, &origin,
                                            nullptr);
            }
            Check(commands->Close(), "Close blit commands");
            ID3D12CommandList* lists[]{commands.Get()};
            pendingBlit = std::move(lifetime);
            submitted = true;
            queue->ExecuteCommandLists(1, lists);
            const UINT64 value = ++fenceValue;
            pendingBlit->fence_value = value;
            Check(queue->Signal(fence.Get(), value), "Signal interop fence");
            pendingBlit->signaled = true;
            ID3D12Fence* fences[]{fence.Get()};
            UINT64 values[]{value};
            Check(interop->ReturnUnderlyingResource(source.Get(), 1, values,
                                                    fences),
                  "ReturnUnderlyingResource");
            returned = true;
            pendingBlit->returned = true;
            waitFence(value);
            pendingBlit.reset();
        } catch (...) {
            HRESULT failure = E_FAIL;
            try {
                throw;
            } catch (const DisplayError& error) {
                failure = error.result();
            } catch (const std::bad_alloc&) {
                failure = E_OUTOFMEMORY;
            } catch (...) {
            }
            if (!returned && !submitted) {
                // Nothing was submitted; close the recording and return the
                // checkout without inventing a signaled GPU dependency.
                commands->Close();
                const auto released = interop->ReturnUnderlyingResource(
                    source.Get(), 0, nullptr, nullptr);
                if (FAILED(released)) {
                    pendingBlit = std::move(lifetime);
                    failure = released;
                }
            }
            if (submitted || pendingBlit) {
                status.backend_failure = failure;
                status.retained_gpu_submission = true;
            }
            throw;
        }
    }

    // Teardown may retire a failed submission, but never make this backend
    // usable again. A fresh successful Signal proves the queue's earlier work
    // if the first Signal failed; timeouts are not treated as completion.
    bool retireFailedSubmission() noexcept {
        if (!pendingBlit)
            return true;
        if (FAILED(device->GetDeviceRemovedReason())) {
            pendingBlit.reset();
            return true;
        }
        auto& pending = *pendingBlit;
        if (!pending.signaled) {
            const auto value = ++fenceValue;
            if (FAILED(queue->Signal(fence.Get(), value)))
                return false;
            pending.fence_value = value;
            pending.signaled = true;
        }
        const auto value = pending.fence_value;
        auto completed = fence->GetCompletedValue();
        if (completed != std::numeric_limits<uint64_t>::max() &&
            completed < value) {
            if (FAILED(fence->SetEventOnCompletion(value, fenceEvent.value)) ||
                WaitForSingleObject(fenceEvent.value, 250) != WAIT_OBJECT_0)
                return false;
            completed = fence->GetCompletedValue();
        }
        if (completed == std::numeric_limits<uint64_t>::max()) {
            // Removal is a teardown disposition, never a successful frame.
            pendingBlit.reset();
            return true;
        }
        if (completed < value)
            return false;
        if (!pending.returned) {
            ID3D12Fence* fences[]{fence.Get()};
            UINT64 values[]{value};
            if (FAILED(pending.interop->ReturnUnderlyingResource(
                    pending.source.Get(), 1, values, fences)))
                return false;
        }
        pendingBlit.reset();
        return true;
    }
};

DisplayBackend::DisplayBackend(HWND window, DisplayPreferences preferences)
    : impl_(std::make_unique<Impl>(window, preferences)) {
    impl_->style(preferences.mode);
}

DisplayBackend::~DisplayBackend() {
    if (impl_ && !impl_->retireFailedSubmission()) {
        impl_->restoreWindow();
        // Completion is unknown. Keep the entire failed command context and
        // resource ownership until process exit instead of freeing in-use GPU
        // objects. This terminal path is never reused for another frame.
        OutputDebugStringW(
            L"TDUR: retaining failed GPU submission until process exit\n");
        (void)impl_.release();
    }
}

void DisplayBackend::set_mode(WindowMode mode) { impl_->setMode(mode); }

void DisplayBackend::move_to_monitor(HMONITOR monitor) {
    impl_->owner();
    const auto target = DescribeMonitor(monitor);
    const auto requested = impl_->preferences.mode;
    if (impl_->swapchain)
        Check(impl_->swapchain->SetFullscreenState(FALSE, nullptr),
              "leave exclusive before monitor migration");
    if (!SetWindowPos(impl_->window, nullptr, target.bounds.left,
                      target.bounds.top,
                      target.bounds.right - target.bounds.left,
                      target.bounds.bottom - target.bounds.top,
                      SWP_NOZORDER | SWP_NOACTIVATE))
        throw DisplayError(HRESULT_FROM_WIN32(GetLastError()),
                           "move to monitor");
    impl_->lastMonitorQuery = 0;
    impl_->nextDeadline = 0;
    impl_->setMode(requested);
}

void DisplayBackend::refresh_monitor() {
    impl_->lastMonitorQuery = 0;
    impl_->refresh();
}

DisplayState DisplayBackend::state() {
    impl_->refresh();
    return impl_->status;
}

void DisplayBackend::wait_for_frame() {
    impl_->refresh();
    impl_->maintainBorderless();
    impl_->enterRequestedExclusiveIfReady();
    const double hz = impl_->status.effective_cap_hz;
    if (hz == 0) {
        impl_->nextDeadline = 0;
        return;
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const int64_t period = std::max<int64_t>(
        1, static_cast<int64_t>(std::llround(impl_->frequency.QuadPart / hz)));
    if (impl_->nextDeadline == 0 ||
        now.QuadPart > impl_->nextDeadline + period) {
        impl_->nextDeadline = now.QuadPart + period;
        return;
    }
    const int64_t remaining = impl_->nextDeadline - now.QuadPart;
    if (remaining > 0) {
        LARGE_INTEGER due{};
        due.QuadPart = -std::max<int64_t>(1, remaining * 10000000 /
                                                 impl_->frequency.QuadPart);
        if (!SetWaitableTimerEx(impl_->pacingTimer.value, &due, 0, nullptr,
                                nullptr, nullptr, 0))
            throw DisplayError(HRESULT_FROM_WIN32(GetLastError()),
                               "SetWaitableTimerEx");
        if (WaitForSingleObject(impl_->pacingTimer.value, 2000) !=
            WAIT_OBJECT_0)
            throw DisplayError(HRESULT_FROM_WIN32(ERROR_TIMEOUT),
                               "frame pacing timer");
    }
    impl_->nextDeadline += period;
}

HWND DisplayBackend::render_window() const { return impl_->sink.value; }
IDirect3D9* DisplayBackend::factory9on12_compat() {
    return impl_->factory9on12Compat();
}
IDirect3D9Ex* DisplayBackend::factory9on12() { return impl_->factory9on12(); }
void DisplayBackend::attach_device9(IDirect3DDevice9* device) {
    impl_->initializeDevice(device);
}

void DisplayBackend::prepare_device_reset() {
    impl_->owner();
    impl_->checkUsable();
    if (impl_->fence && impl_->fenceValue)
        impl_->waitFence(impl_->fenceValue);
    // Real attachment retains no D3D9 resource. Only the optional source-owned
    // reference fixture has a target to unbind here.
    if (impl_->referenceTarget) {
        ComPtr<IDirect3DSurface9> backbuffer;
        Check(impl_->device9->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO,
                                            &backbuffer),
              "GetBackBuffer before reference Reset");
        Check(impl_->device9->SetRenderTarget(0, backbuffer.Get()),
              "unbind reference target before Reset");
        impl_->referenceTarget.Reset();
    }
    impl_->nextDeadline = 0;
}

void DisplayBackend::complete_device_reset() {
    impl_->owner();
    impl_->checkUsable();
    if (!impl_->device9)
        throw DisplayError(E_UNEXPECTED,
                           "Reset completion needs a live device");
    ComPtr<IDirect3DDevice9On12> interop;
    Check(impl_->device9->QueryInterface(IID_PPV_ARGS(&interop)),
          "QueryInterface interop after Reset");
    ComPtr<ID3D12Device> device;
    Check(interop->GetD3D12Device(IID_PPV_ARGS(&device)),
          "GetD3D12Device after Reset");
    ComPtr<IUnknown> before;
    ComPtr<IUnknown> after;
    Check(impl_->device.As(&before), "pre-Reset device identity");
    Check(device.As(&after), "post-Reset device identity");
    if (before.Get() != after.Get())
        throw DisplayError(DXGI_ERROR_DEVICE_RESET,
                           "D3D12 device identity changed across Reset");
    impl_->nextDeadline = 0;
    refresh_monitor();
}

void DisplayBackend::abandon_window() noexcept {
    // Safe during reentrant WM_NCDESTROY: GPU teardown waits until the host's
    // active operation exits. Mark the HWND unusable without calling DXGI.
    impl_->window = nullptr;
}

void DisplayBackend::create_reference_device(uint32_t width, uint32_t height) {
    impl_->owner();
    if (width == 0 || height == 0 || width > 16384 || height > 16384)
        throw std::invalid_argument(
            "reference dimensions must be in [1,16384]");
    auto* factory = factory9on12();
    D3DPRESENT_PARAMETERS parameters{};
    parameters.BackBufferWidth = width;
    parameters.BackBufferHeight = height;
    parameters.BackBufferFormat = D3DFMT_A8R8G8B8;
    parameters.BackBufferCount = 1;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = impl_->sink.value;
    parameters.Windowed = TRUE;
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    ComPtr<IDirect3DDevice9Ex> created;
    Check(factory->CreateDeviceEx(
              D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, impl_->window,
              D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED |
                  D3DCREATE_FPU_PRESERVE,
              &parameters, nullptr, &created),
          "CreateDeviceEx 9On12 reference");
    attach_device9(created.Get());
    impl_->reference9 = std::move(created);
    D3DMULTISAMPLE_TYPE msaa = D3DMULTISAMPLE_NONE;
    DWORD quality = 0;
    if (impl_->preferences.maximum_reference_quality) {
        for (int count = 16; count >= 2; --count) {
            DWORD levels = 0;
            const auto candidate = static_cast<D3DMULTISAMPLE_TYPE>(count);
            if (SUCCEEDED(factory->CheckDeviceMultiSampleType(
                    D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, TRUE,
                    candidate, &levels)) &&
                levels > 0) {
                msaa = candidate;
                quality = levels - 1;
                break;
            }
        }
        if (impl_->status.maximum_anisotropy > 1) {
            Check(
                impl_->device9->SetSamplerState(
                    0, D3DSAMP_MAXANISOTROPY, impl_->status.maximum_anisotropy),
                "reference anisotropy capability");
        }
    }
    Check(impl_->device9->CreateRenderTarget(width, height, D3DFMT_A8R8G8B8,
                                             msaa, quality, FALSE,
                                             &impl_->referenceTarget, nullptr),
          "CreateRenderTarget reference");
    Check(impl_->device9->SetRenderTarget(0, impl_->referenceTarget.Get()),
          "SetRenderTarget reference");
    impl_->status.reference_msaa_samples =
        msaa == D3DMULTISAMPLE_NONE ? 1 : static_cast<uint32_t>(msaa);
}

IDirect3DDevice9Ex* DisplayBackend::reference_device() const {
    return impl_->reference9.Get();
}
IDirect3DSurface9* DisplayBackend::reference_target() const {
    return impl_->referenceTarget.Get();
}
ID3D12Device* DisplayBackend::device12() const { return impl_->device.Get(); }

DisplayPixels DisplayBackend::readback_surface9(IDirect3DSurface9* surface) {
    impl_->owner();
    impl_->checkUsable();
    if (!surface || !impl_->device)
        throw std::invalid_argument("readback requires an attached surface");
    D3DSURFACE_DESC original{};
    Check(surface->GetDesc(&original), "GetDesc readback surface");
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = original.Width;
    desc.Height = original.Height;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> output;
    Check(impl_->device->CreateCommittedResource(
              &heap, D3D12_HEAP_FLAG_NONE, &desc,
              D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
              IID_PPV_ARGS(&output)),
          "CreateCommittedResource readback color");
    auto rtv = impl_->rtvs->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += 2 * impl_->rtvIncrement;
    impl_->device->CreateRenderTargetView(output.Get(), nullptr, rtv);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 bytes = 0;
    impl_->device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr,
                                         nullptr, &bytes);
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = bytes;
    buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    ComPtr<ID3D12Resource> readback;
    Check(impl_->device->CreateCommittedResource(
              &heap, D3D12_HEAP_FLAG_NONE, &buffer,
              D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)),
          "CreateCommittedResource readback buffer");
    impl_->blit(surface, output.Get(), rtv, original.Width, original.Height,
                D3D12_RESOURCE_STATE_COPY_SOURCE, readback.Get(), &footprint);
    void* mappedAddress = nullptr;
    const D3D12_RANGE range{0, static_cast<SIZE_T>(bytes)};
    Check(readback->Map(0, &range, &mappedAddress), "Map D3D12 readback");
    const auto* mapped = static_cast<const std::byte*>(mappedAddress);
    DisplayPixels result;
    result.width = original.Width;
    result.height = original.Height;
    result.rgba.resize(static_cast<size_t>(result.width) * result.height * 4);
    for (uint32_t row = 0; row < result.height; ++row) {
        const auto* source =
            mapped + footprint.Offset +
            static_cast<size_t>(row) * footprint.Footprint.RowPitch;
        auto* target =
            result.rgba.data() + static_cast<size_t>(row) * result.width * 4;
        for (uint32_t column = 0; column < result.width; ++column) {
            target[column * 4] = source[column * 4 + 2];
            target[column * 4 + 1] = source[column * 4 + 1];
            target[column * 4 + 2] = source[column * 4];
            target[column * 4 + 3] = source[column * 4 + 3];
        }
    }
    const D3D12_RANGE noWrite{0, 0};
    readback->Unmap(0, &noWrite);
    return result;
}

HRESULT DisplayBackend::present_surface9(IDirect3DSurface9* surface) {
    impl_->owner();
    impl_->checkUsable();
    if (!surface || !impl_->status.d3d9on12_verified)
        throw std::invalid_argument(
            "presentation requires an attached surface");
    impl_->refresh();
    impl_->enterRequestedExclusiveIfReady();
    impl_->ensureSwapchain();
    const UINT index = impl_->swapchain->GetCurrentBackBufferIndex();
    auto rtv = impl_->rtvs->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(index) * impl_->rtvIncrement;
    impl_->blit(surface, impl_->buffers[index].Get(), rtv, impl_->swapWidth,
                impl_->swapHeight, D3D12_RESOURCE_STATE_PRESENT, nullptr,
                nullptr, D3D12_RESOURCE_STATE_PRESENT);
    impl_->status.last_present = SelectPresentationPolicy(
        impl_->status.exclusive_active, impl_->status.tearing_supported,
        impl_->preferences.allow_tearing, impl_->preferences.vsync);
    const auto policy = impl_->status.last_present;
    return impl_->swapchain->Present(policy.sync_interval,
                                     policy.present_flags);
}

std::vector<MonitorState> DisplayBackend::monitors() {
    std::vector<HMONITOR> handles;
    if (!EnumDisplayMonitors(nullptr, nullptr, CollectMonitor,
                             reinterpret_cast<LPARAM>(&handles)))
        throw DisplayError(HRESULT_FROM_WIN32(GetLastError()),
                           "EnumDisplayMonitors");
    std::vector<MonitorState> result;
    result.reserve(handles.size());
    for (const auto monitor : handles)
        result.push_back(DescribeMonitor(monitor));
    return result;
}

} // namespace tdur::mod
