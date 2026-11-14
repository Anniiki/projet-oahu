// Holds the loader lock for the hang-loader witness mode. Its
// process attach signals Local\TDUR-CRASH-loader-<pid>, then never returns.
#include <windows.h>

#include <cwchar>

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t name[64]{};
        std::swprintf(name, 64, L"Local\\TDUR-CRASH-loader-%lu",
                      GetCurrentProcessId());
        const HANDLE held = OpenEventW(EVENT_MODIFY_STATE, FALSE, name);
        if (held != nullptr)
            SetEvent(held);
        Sleep(INFINITE);
    }
    return TRUE;
}
