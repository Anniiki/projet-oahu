#include "tdur/mod/display_startup_gate.hpp"

#include "tdur/mod/display_hooks.hpp"
#include "tdur/mod/observation_hooks.hpp"
#include "tdur/mod/retail_background.hpp"
#include "tdur/mod/retail_physics_context.hpp"
#include "tdur/mod/retail_video_defaults.hpp"

#include <detours.h>

#include <array>
#include <bit>
#include <cwchar>
#include <string_view>

namespace {
using Factory = IDirect3D9*(WINAPI*)(UINT);
Factory originalFactory = nullptr;
INIT_ONCE initializeOnce = INIT_ONCE_STATIC_INIT;
HRESULT initializationResult = E_UNEXPECTED;
bool maximumGraphics = false;
bool backgroundSimulation = false;
bool physicsContext = false;

HRESULT DeviceCreated(IDirect3DDevice9* device) noexcept {
    if (backgroundSimulation) {
        const auto applied = TdurApplyInitialBackgroundPolicy(device);
        if (FAILED(applied))
            return applied;
    }
    return maximumGraphics ? TdurApplyInitialVideoDefaults(device) : S_OK;
}

void FirstFrame() noexcept {
    if (maximumGraphics)
        TdurSealInitialVideoDefaults();
}

void Log(const char* event, HRESULT result, void*) noexcept {
    TdurObservationRecord(event, static_cast<DWORD>(result));
}

BOOL CALLBACK InitializeDisplay(PINIT_ONCE, PVOID, PVOID*) noexcept {
    tdur::mod::DisplayHookOptions options;
    options.enabled = true;
    options.install_factory_detour = false;
    options.log = Log;
    if (physicsContext) {
        TdurObservePhysicsTimerBinding();
        options.on_presented_frame = TdurFlushPhysicsContext;
    }
    std::array<wchar_t, 4> resourceMetadata{};
    const auto resourceMetadataLength = GetEnvironmentVariableW(
        L"TDUR_GRAPHICS_METADATA", resourceMetadata.data(),
        static_cast<DWORD>(resourceMetadata.size()));
    if (resourceMetadataLength == 1 && resourceMetadata[0] == L'1')
        options.observe_resource_metadata = true;
    else if (resourceMetadataLength &&
             !(resourceMetadataLength == 1 && resourceMetadata[0] == L'0')) {
        initializationResult = E_INVALIDARG;
        return TRUE;
    }
    std::array<wchar_t, 4> maximum{};
    const auto maximumLength =
        GetEnvironmentVariableW(L"TDUR_RETAIL_MAX_GRAPHICS", maximum.data(),
                                static_cast<DWORD>(maximum.size()));
    if (maximumLength == 1 && maximum[0] == L'1') {
        maximumGraphics = true;
    } else if (maximumLength && !(maximumLength == 1 && maximum[0] == L'0')) {
        initializationResult = E_INVALIDARG;
        return TRUE;
    }
    std::array<wchar_t, 4> background{};
    const auto backgroundLength =
        GetEnvironmentVariableW(L"TDUR_RETAIL_BACKGROUND", background.data(),
                                static_cast<DWORD>(background.size()));
    if (backgroundLength == 1 && background[0] == L'1')
        backgroundSimulation = true;
    else if (backgroundLength &&
             !(backgroundLength == 1 && background[0] == L'0')) {
        initializationResult = E_INVALIDARG;
        return TRUE;
    }
    if (maximumGraphics || backgroundSimulation) {
        options.on_device_created = DeviceCreated;
        options.on_first_frame = FirstFrame;
    }
    std::array<wchar_t, 32> text{};
    const auto length = GetEnvironmentVariableW(
        L"TDUR_DISPLAY_MODE", text.data(), static_cast<DWORD>(text.size()));
    if (length >= text.size()) {
        initializationResult = E_INVALIDARG;
        return TRUE;
    }
    const std::wstring_view mode(text.data(), length);
    if (mode.empty() || mode == L"borderless")
        options.display.mode = tdur::mod::WindowMode::borderless;
    else if (mode == L"windowed")
        options.display.mode = tdur::mod::WindowMode::windowed;
    else if (mode == L"exclusive")
        options.display.mode = tdur::mod::WindowMode::exclusive;
    else {
        initializationResult = E_INVALIDARG;
        return TRUE;
    }
    initializationResult = tdur::mod::InstallDisplayHooks(options);
    TdurObservationRecord("display-deferred-initialization",
                          static_cast<DWORD>(initializationResult));
    return TRUE;
}

IDirect3D9* WINAPI FactoryGate(UINT sdk_version) noexcept {
    if (!InitOnceExecuteOnce(&initializeOnce, InitializeDisplay, nullptr,
                             nullptr) ||
        FAILED(initializationResult)) {
        TdurObservationRecord("display-deferred-initialization-failed",
                              static_cast<DWORD>(initializationResult));
        return nullptr;
    }
    return tdur::mod::CreateDisplayFactory9(sdk_version);
}
} // namespace

bool TdurInstallDisplayStartupGate() noexcept {
    std::array<wchar_t, 4> enabled{};
    const auto length =
        GetEnvironmentVariableW(L"TDUR_D3D12_DISPLAY", enabled.data(),
                                static_cast<DWORD>(enabled.size()));
    if (length == 0 || (length == 1 && enabled[0] == L'0'))
        return true;
    if (length != 1 || enabled[0] != L'1') {
        TdurObservationRecord("display-gate-invalid-selection",
                              ERROR_INVALID_PARAMETER);
        return false;
    }
    // The executable's static imports are already loaded at process attach.
    // Never LoadLibrary, create a device, enumerate/suspend worker threads or
    // run COM/window setup while the loader lock is held.
    const auto module = GetModuleHandleW(L"d3d9.dll");
    if (!module) {
        TdurObservationRecord("display-gate-d3d9-not-loaded",
                              ERROR_MOD_NOT_FOUND);
        return false;
    }
    const auto factory = GetProcAddress(module, "Direct3DCreate9");
    if (!factory) {
        TdurObservationRecord("display-gate-factory-not-found", GetLastError());
        return false;
    }
    originalFactory = std::bit_cast<Factory>(factory);
    std::array<wchar_t, 4> physics{};
    const auto physicsLength =
        GetEnvironmentVariableW(L"TDUR_PHYSICS_CONTEXT", physics.data(),
                                static_cast<DWORD>(physics.size()));
    if (physicsLength == 1 && physics[0] == L'1')
        physicsContext = true;
    else if (physicsLength && !(physicsLength == 1 && physics[0] == L'0'))
        return false;
    LONG result = DetourTransactionBegin();
    if (result != NO_ERROR)
        return false;
    result = DetourUpdateThread(GetCurrentThread());
    if (result == NO_ERROR)
        result =
            DetourAttach(&originalFactory, static_cast<Factory>(FactoryGate));
    bool rendererQueued = false;
    if (result == NO_ERROR) {
        std::array<wchar_t, 4> background{};
        const auto backgroundLength = GetEnvironmentVariableW(
            L"TDUR_RETAIL_BACKGROUND", background.data(),
            static_cast<DWORD>(background.size()));
        if (backgroundLength == 1 && background[0] == L'1')
            result = TdurQueueRetailRendererGate(rendererQueued);
    }
    bool physicsQueued = false;
    if (result == NO_ERROR && physicsContext)
        result = TdurQueueRetailPhysicsContextGate(physicsQueued);
    if (result == NO_ERROR)
        result = DetourTransactionCommit();
    else
        DetourTransactionAbort();
    TdurObservationRecord("display-public-factory-gate-armed",
                          static_cast<DWORD>(result));
    if (result == NO_ERROR && rendererQueued && !TdurVerifyRetailRendererGate())
        result = ERROR_WRITE_FAULT;
    if (result == NO_ERROR && physicsQueued &&
        !TdurVerifyRetailPhysicsContextGate())
        result = ERROR_WRITE_FAULT;
    if (physicsQueued)
        TdurObservationRecord("physics-context-gate-armed-pre-entry",
                              static_cast<DWORD>(result));
    if (rendererQueued)
        TdurObservationRecord("retail-renderer-gate-armed-pre-entry",
                              static_cast<DWORD>(result));
    return result == NO_ERROR;
}
