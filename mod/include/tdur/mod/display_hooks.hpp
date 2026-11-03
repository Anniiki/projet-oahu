#pragma once

#include "tdur/mod/display_backend.hpp"

namespace tdur::mod {

using DisplayLog = void (*)(const char* event, HRESULT result,
                            void* context) noexcept;

struct DisplayHookOptions {
    bool enabled = false;
    // A pre-entry public-factory gate can own that one detour separately.
    // Full factory/device setup still runs outside the loader lock.
    bool install_factory_detour = true;
    // Bounded resource descriptions only: no pixels or retained COM references.
    bool observe_resource_metadata = false;
    // Presentation dimensions follow the output; render dimensions remain
    // the host engine's responsibility.
    DisplayPreferences display;
    HRESULT (*on_device_created)(IDirect3DDevice9*) noexcept = nullptr;
    void (*on_first_frame)() noexcept = nullptr;
    // Successful DXGI presentations only, on the owner HWND thread.
    // Diagnostic callback; do not reenter rendering or release the device.
    void (*on_presented_frame)() noexcept = nullptr;
    DisplayLog log = nullptr;
    void* log_context = nullptr;
};

struct DisplayHookReport {
    bool installed = false;
    uint64_t factories_on12{};
    uint64_t devices_created{};
    uint64_t tracked_devices{};
    uint64_t resets_succeeded{};
    uint64_t resets_failed{};
    uint64_t presents{};
    uint64_t present_failures{};
    uint64_t occluded{};
    uint64_t resource_creation_calls{};
    uint64_t resource_creation_successes{};
};

// Explicitly call outside DllMain, before the first Direct3DCreate9 call.
// No loader-lock GPU work, fake focus event or internal game patch is used.
// enabled=false performs no installation or runtime load and returns S_FALSE.
HRESULT InstallDisplayHooks(const DisplayHookOptions&) noexcept;

// For a separately armed factory gate, after successful explicit installation.
// Never loads/creates the graphics runtime while running under DllMain.
IDirect3D9* CreateDisplayFactory9(UINT sdk_version) noexcept;

// Call at an application-owned quiescent point, after all intercepted COM
// objects are released and the HWND thread has pumped its pending cleanup.
// Returns ERROR_BUSY if live/tracked objects prevent a safe detach.
HRESULT UninstallDisplayHooks() noexcept;
DisplayHookReport DisplayHooksReport() noexcept;

// Owner-HWND-thread diagnostics/control. They preserve the real COM object;
// resources' GetDevice, QI identity and native reference counts are unchanged.
HRESULT DisplayDeviceState9(IDirect3DDevice9*, DisplayState&) noexcept;
HRESULT SetDisplayDeviceMode9(IDirect3DDevice9*, WindowMode) noexcept;
HRESULT ReadDisplayBackbuffer9(IDirect3DDevice9*, DisplayPixels&) noexcept;

} // namespace tdur::mod
