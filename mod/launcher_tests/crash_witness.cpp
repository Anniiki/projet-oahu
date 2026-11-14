// Witness that crashes or hangs on purpose, under the real launcher
// and the real observer DLL, or alone for a reference run. The mode comes
// from TDUR_CRASH_WITNESS_MODE. Files go to TDUR_PRIVATE_DOCUMENTS (or to
// TDUR_CRASH_WITNESS_DIR for a direct run): crash-witness.json before acting,
// crash-heartbeat.bin (sentinel thread counter, every 50 ms) and
// crash-filter.json from the witness's own top-level filter. That filter
// returns EXCEPTION_EXECUTE_HANDLER, so Windows writes no WER report.
#include <windows.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace {
std::wstring directory;
std::wstring dump_expected;
std::atomic<DWORD> worker_tid{0};
void* fault_address = nullptr;
void* target_address = nullptr;
void* sentinel_page = nullptr;
HWND window = nullptr;

std::wstring Environment(const wchar_t* name) {
    std::array<wchar_t, 1024> value{};
    const DWORD length = GetEnvironmentVariableW(
        name, value.data(), static_cast<DWORD>(value.size()));
    return length != 0 && length < value.size() ? value.data() : std::wstring();
}

void WriteText(const wchar_t* name, const std::string& text) {
    const auto path = directory + L"\\" + name;
    const HANDLE file =
        CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written,
              nullptr);
    CloseHandle(file);
}

std::string Address(const void* value) {
    char text[24]{};
    std::snprintf(text, sizeof(text), "\"0x%llx\"",
                  static_cast<unsigned long long>(
                      reinterpret_cast<std::uintptr_t>(value)));
    return text;
}

void WriteFacts() {
    DWORD flags = 0;
    BOOL permanent = FALSE;
    const BOOL dep =
        GetProcessDEPPolicy(GetCurrentProcess(), &flags, &permanent);
    WriteText(L"crash-witness.json",
              "{\"pid\":" + std::to_string(GetCurrentProcessId()) +
                  ",\"main_tid\":" + std::to_string(GetCurrentThreadId()) +
                  ",\"worker_tid\":" + std::to_string(worker_tid.load()) +
                  ",\"fault_address\":" + Address(fault_address) +
                  ",\"target_address\":" + Address(target_address) +
                  ",\"sentinel_page\":" + Address(sentinel_page) +
                  ",\"dep_known\":" + (dep ? "true" : "false") +
                  ",\"dep_flags\":" + std::to_string(flags) + "}\n");
}

// Top-level filter: runs inside the real last-chance filter, after the
// observer's detour. It records whether the crash dump already exists.
LONG WINAPI WitnessFilter(EXCEPTION_POINTERS* info) {
    bool dump_ready = false;
    if (!dump_expected.empty()) {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        dump_ready = GetFileAttributesExW(dump_expected.c_str(),
                                          GetFileExInfoStandard, &data) &&
                     (data.nFileSizeLow != 0 || data.nFileSizeHigh != 0);
    }
    char text[160]{};
    std::snprintf(text, sizeof(text),
                  "{\"filter_called\":true,\"code\":%lu,\"thread\":%lu,"
                  "\"dump_ready\":%s}\n",
                  info && info->ExceptionRecord
                      ? info->ExceptionRecord->ExceptionCode
                      : 0UL,
                  GetCurrentThreadId(), dump_ready ? "true" : "false");
    WriteText(L"crash-filter.json", text);
    return EXCEPTION_EXECUTE_HANDLER;
}

DWORD WINAPI Heartbeat(void*) {
    const auto path = directory + L"\\crash-heartbeat.bin";
    const HANDLE file = CreateFileW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return 1;
    for (std::uint32_t beat = 1;; ++beat) {
        LARGE_INTEGER start{};
        DWORD written = 0;
        SetFilePointerEx(file, start, nullptr, FILE_BEGIN);
        WriteFile(file, &beat, sizeof(beat), &written, nullptr);
        Sleep(50);
    }
}

LRESULT CALLBACK Procedure(HWND handle, UINT message, WPARAM w, LPARAM l) {
    return DefWindowProcW(handle, message, w, l);
}

void CreateWitnessWindow() {
    WNDCLASSW type{};
    type.lpfnWndProc = Procedure;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = L"TdurCrashWitness";
    RegisterClassW(&type);
    window =
        CreateWindowExW(0, type.lpszClassName, L"", WS_POPUP | WS_VISIBLE, 0, 0,
                        16, 16, nullptr, nullptr, type.hInstance, nullptr);
}

void Pump(DWORD milliseconds) {
    const ULONGLONG until = GetTickCount64() + milliseconds;
    while (GetTickCount64() < until) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(10);
    }
}

void* PrivateSentinel() {
    // Private read-write page with a known pattern: executing it is a DEP
    // violation in private memory, like a crash at a non-image address.
    auto* page = static_cast<char*>(
        VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    for (std::size_t offset = 0; offset + 16 <= 4096; offset += 16)
        std::memcpy(page + offset, "TDUR-DUMP-SENTRY", 16);
    return page;
}

DWORD WINAPI ExecutePrivate(void*) {
    worker_tid = GetCurrentThreadId();
    WriteFacts();
    reinterpret_cast<void (*)()>(fault_address)();
    return 0;
}

#pragma warning(push)
#pragma warning(disable : 4717) // Recursion on purpose.
int Recurse(int depth) {
    volatile char frame[512];
    frame[depth % 512] = static_cast<char>(depth);
    return Recurse(depth + 1) + frame[0];
}
#pragma warning(pop)

DWORD WINAPI Overflow(void*) {
    worker_tid = GetCurrentThreadId();
    WriteFacts();
    return static_cast<DWORD>(Recurse(0));
}

CRITICAL_SECTION section;
DWORD WINAPI HoldSection(void* ready) {
    worker_tid = GetCurrentThreadId();
    EnterCriticalSection(&section);
    SetEvent(static_cast<HANDLE>(ready));
    Sleep(INFINITE);
    return 0;
}

DWORD WINAPI LoadHolder(void* path) {
    worker_tid = GetCurrentThreadId();
    LoadLibraryW(static_cast<const wchar_t*>(path));
    return 0;
}

int HandledFault() {
    __try {
        *static_cast<volatile int*>(nullptr) = 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 37;
    }
    return 38;
}

int ImageCall() {
    __try {
        RaiseException(0xE0481010UL, 0, 0, nullptr);
    } __except (UnhandledExceptionFilter(GetExceptionInformation())) {
        return 37;
    }
    return 37;
}

void WaitThread(HANDLE thread) { WaitForSingleObject(thread, INFINITE); }
} // namespace

int wmain() {
    const std::wstring mode = Environment(L"TDUR_CRASH_WITNESS_MODE");
    directory = Environment(L"TDUR_PRIVATE_DOCUMENTS");
    if (directory.empty())
        directory = Environment(L"TDUR_CRASH_WITNESS_DIR");
    dump_expected = Environment(L"TDUR_CRASH_WITNESS_DUMP");
    if (directory.empty() || mode.empty())
        return 90;
    if (mode == L"exit-normal") {
        WriteFacts();
        return 37;
    }
    SetUnhandledExceptionFilter(WitnessFilter);
    CreateThread(nullptr, 0, Heartbeat, nullptr, 0, nullptr);
    CreateWitnessWindow();
    Pump(2000);
    if (mode == L"crash-exec-private") {
        sentinel_page = PrivateSentinel();
        fault_address = static_cast<char*>(sentinel_page) + 0x100;
        target_address = fault_address;
        WaitThread(
            CreateThread(nullptr, 0, ExecutePrivate, nullptr, 0, nullptr));
    } else if (mode == L"crash-write-reserved") {
        target_address =
            VirtualAlloc(nullptr, 65536, MEM_RESERVE, PAGE_READWRITE);
        WriteFacts();
        *static_cast<volatile int*>(target_address) = 1;
    } else if (mode == L"crash-stack-overflow") {
        WaitThread(CreateThread(nullptr, 0, Overflow, nullptr, 0, nullptr));
    } else if (mode == L"crash-handled") {
        WriteFacts();
        return HandledFault();
    } else if (mode == L"uef-image-call") {
        WriteFacts();
        return ImageCall();
    } else if (mode == L"hang-ui" || mode == L"idle-blocked") {
        WriteFacts();
        Sleep(INFINITE);
    } else if (mode == L"hang-ui-recover") {
        // Review F1: hung long enough for the question (asked at 7 s with
        // test thresholds), then the window answers again and the witness
        // exits by itself, since a play session has no deadline.
        WriteFacts();
        Sleep(15000);
        Pump(1000);
        return 37;
    } else if (mode == L"hang-cross-thread") {
        InitializeCriticalSection(&section);
        const HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        CreateThread(nullptr, 0, HoldSection, ready, 0, nullptr);
        WaitForSingleObject(ready, INFINITE);
        WriteFacts();
        EnterCriticalSection(&section);
    } else if (mode == L"hang-loader") {
        const auto dll = Environment(L"TDUR_CRASH_LOADER_HOLD_DLL");
        std::array<wchar_t, 64> name{};
        std::swprintf(name.data(), name.size(), L"Local\\TDUR-CRASH-loader-%lu",
                      GetCurrentProcessId());
        const HANDLE held = CreateEventW(nullptr, TRUE, FALSE, name.data());
        CreateThread(nullptr, 0, LoadHolder, const_cast<wchar_t*>(dll.c_str()),
                     0, nullptr);
        if (WaitForSingleObject(held, 10000) != WAIT_OBJECT_0)
            return 91;
        WriteFacts();
        LoadLibraryW(L"version.dll");
    } else if (mode == L"busy-pumping") {
        WriteFacts();
        const ULONGLONG until = GetTickCount64() + 25000;
        while (GetTickCount64() < until) {
            const ULONGLONG slice = GetTickCount64() + 200;
            while (GetTickCount64() < slice) {
            }
            Pump(1);
        }
        return 37;
    } else if (mode == L"stutter") {
        WriteFacts();
        for (int index = 0; index < 4; ++index) {
            Sleep(4000);
            Pump(1000);
        }
        return 37;
    } else if (mode == L"idle") {
        WriteFacts();
        Pump(INFINITE);
    } else {
        return 92;
    }
    return 93;
}
