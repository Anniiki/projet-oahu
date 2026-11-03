#include <shlobj.h>
#include <windows.h>

#include <array>
#include <bit>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {
bool pre_main_ok = false;

struct EarlyDocuments {
    EarlyDocuments() {
        std::array<wchar_t, MAX_PATH> expected{};
        const auto length =
            GetEnvironmentVariableW(L"TDUR_PRIVATE_DOCUMENTS", expected.data(),
                                    static_cast<DWORD>(expected.size()));
        if (length == 0 || length >= expected.size()) {
            return;
        }
        const auto shell = LoadLibraryExW(L"shell32.dll", nullptr,
                                          LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (shell == nullptr) {
            return;
        }
        using Folder = HRESULT(WINAPI*)(HWND, int, HANDLE, DWORD, LPSTR);
        const auto symbol = GetProcAddress(shell, "SHGetFolderPathA");
        if (symbol == nullptr) {
            return;
        }
        const auto folder = std::bit_cast<Folder>(symbol);
        std::array<char, MAX_PATH> returned{};
        std::array<char, MAX_PATH> expected_ansi{};
        if (WideCharToMultiByte(CP_ACP, 0, expected.data(), -1,
                                expected_ansi.data(),
                                static_cast<int>(expected_ansi.size()), nullptr,
                                nullptr) == 0 ||
            FAILED(
                folder(nullptr, CSIDL_PERSONAL, nullptr, 0, returned.data())) ||
            std::strcmp(returned.data(), expected_ansi.data()) != 0) {
            return;
        }
        const auto marker = std::filesystem::path(expected.data()) /
                            L"injected-before-main.txt";
        const auto handle =
            CreateFileW(marker.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            return;
        }
        constexpr char message[] = "pre-main\n";
        DWORD written = 0;
        pre_main_ok = WriteFile(handle, message, sizeof(message) - 1, &written,
                                nullptr) &&
                      written == sizeof(message) - 1;
        CloseHandle(handle);
    }
} early_documents;
} // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc == 3 && std::wstring_view(argv[1]) == L"--late-load") {
        const auto loaded =
            LoadLibraryExW(argv[2], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (loaded) {
            FreeLibrary(loaded);
            return 21;
        }
        return GetLastError() == ERROR_DLL_INIT_FAILED ? 0 : 22;
    }
    std::array<wchar_t, 4> verified{};
    if (GetEnvironmentVariableW(L"TDUR_RETAIL_EXECUTABLE_VERIFIED",
                                verified.data(),
                                static_cast<DWORD>(verified.size())) != 1 ||
        verified[0] != L'0')
        return 23;
    if (!pre_main_ok) {
        return 12;
    }
    if (argc != 1) {
        if (argc != 3 || std::wstring_view(argv[1]) != L"-system")
            return 20;
        std::ifstream config(std::filesystem::path(argv[2]), std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(config)), {});
        if (contents != "native private video fixture\r\n")
            return 21;
    }
    std::array<wchar_t, 32768> directory{};
    if (GetEnvironmentVariableW(L"TDUR_SOURCE_DOCUMENTS", directory.data(),
                                static_cast<DWORD>(directory.size())) == 0) {
        return 10;
    }
    const auto source =
        std::filesystem::path(directory.data()) / L"saves" / L"canary.bin";
    const HANDLE forbidden =
        CreateFileW(source.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, 0, nullptr);
    if (forbidden != INVALID_HANDLE_VALUE) {
        CloseHandle(forbidden);
        return 11;
    }
    std::array<wchar_t, 16> mode{};
    GetEnvironmentVariableW(L"TDUR_LAUNCH_FIXTURE_MODE", mode.data(),
                            static_cast<DWORD>(mode.size()));
    if (std::wstring(mode.data()) == L"abnormal-code") {
        // Deliberate exit status, not an actual fault or an exception fixture.
        ExitProcess(0xc0000005UL);
    }
    if (std::wstring(mode.data()) == L"timeout") {
        const auto window = CreateWindowExW(
            0, L"STATIC", L"TDUR_WITNESS_PRIVATE_DIAGNOSTIC_TEXT", WS_POPUP, 0,
            0, 16, 16, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window)
            return 24;
        ShowWindow(window, SW_SHOWNOACTIVATE);
        Sleep(30000);
    }
    if (std::wstring(mode.data()) == L"play") {
        const auto window = CreateWindowExW(
            0, L"STATIC", L"Source-owned private play text", WS_POPUP, 0, 0, 16,
            16, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!window)
            return 22;
        ShowWindow(window, SW_SHOWNOACTIVATE);
        Sleep(1400); // Exceeds the diagnostic one-second deadline.
        DestroyWindow(window);
    }
    return 37;
}
