// Observation infrastructure: no game instructions, login or protection hooks.
// The only public export forwards the real system DirectInput8Create ABI.
#define DIRECTINPUT_VERSION 0x0800
#include "tdur/mod/cursor_diagnostics.hpp"
#include "tdur/mod/display_startup_gate.hpp"
#include "tdur/mod/observation_hooks.hpp"
#include <detours.h>
#include <dinput.h>
#include <windows.h>

#include <array>
#include <bit>
#include <cstdio>
#include <cstring>

namespace {
using Factory = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
HMODULE selfModule = nullptr;
HMODULE systemModule = nullptr;
Factory systemFactory = nullptr;
DWORD initializationError = ERROR_SUCCESS;
INIT_ONCE initializeOnce = INIT_ONCE_STATIC_INIT;

void Log(const char* event, HRESULT result) noexcept {
    TdurObservationRecord(event, DWORD(result));
}

BOOL CALLBACK InitializeSystemFactory(PINIT_ONCE, PVOID, PVOID*) noexcept {
    wchar_t path[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(path, MAX_PATH);
    constexpr wchar_t filename[] = L"\\dinput8.dll";
    if (length == 0 || length + std::size(filename) > MAX_PATH) {
        initializationError = ERROR_INSUFFICIENT_BUFFER;
        return TRUE;
    }
    std::memcpy(path + length, filename, sizeof(filename));
    systemModule = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (systemModule == nullptr || systemModule == selfModule) {
        initializationError =
            systemModule == selfModule ? ERROR_INVALID_DLL : GetLastError();
        return TRUE;
    }
    const auto symbol = GetProcAddress(systemModule, "DirectInput8Create");
    if (symbol == nullptr) {
        initializationError = GetLastError();
        return TRUE;
    }
    systemFactory = std::bit_cast<Factory>(symbol);
    Log("system-factory-resolved", S_OK);
    return TRUE;
}
} // namespace

extern "C" HRESULT WINAPI TdurDirectInput8Create(HINSTANCE instance,
                                                 DWORD version, REFIID iid,
                                                 LPVOID* output,
                                                 LPUNKNOWN outer) noexcept {
    if (!InitOnceExecuteOnce(&initializeOnce, InitializeSystemFactory, nullptr,
                             nullptr) ||
        systemFactory == nullptr) {
        if (output != nullptr) {
            *output = nullptr;
        }
        const HRESULT failure = HRESULT_FROM_WIN32(
            initializationError != ERROR_SUCCESS ? initializationError
                                                 : ERROR_DLL_INIT_FAILED);
        Log("system-factory-unavailable", failure);
        return failure;
    }
    const HRESULT result = systemFactory(instance, version, iid, output, outer);
    Log("DirectInput8Create-forwarded", result);
    return result;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID reserved) {
    if (DetourIsHelperProcess()) {
        return TRUE;
    }
    if (reason == DLL_PROCESS_ATTACH) {
        // The owned launcher injects an initial import, before the entrypoint.
        // Reject late LoadLibrary before installing any hook.
        if (reserved == nullptr)
            return FALSE;
        selfModule = module;
        DisableThreadLibraryCalls(module);
        DetourRestoreAfterWith();
        return TdurInstallObservationHooks() &&
                       TdurInstallDisplayStartupGate() &&
                       TdurInstallCursorDiagnostics()
                   ? TRUE
                   : FALSE;
    }
    // No worker thread, COM initialization or system DirectInput load here.
    // All hooks and their fixed configuration remain for the process lifetime.
    return TRUE;
}
