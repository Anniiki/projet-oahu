// Process witness of the Shell-free save folder deletion (new
// policy). Loaded with the observation DLL by a static import of
// DirectInput8Create, it calls SHFileOperationA and SHFileOperationW on the
// synthetic trees that run_shell_delete_fixture.py prepared, and prints one
// line per case. Every call runs on its own thread and is awaited ten seconds
// at most: a forwarded call reaches the real Shell, which may block. When a
// call has not returned, the results are printed and the process ends itself
// with TerminateProcess, without detaching the DLLs.
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <objbase.h>
#include <shellapi.h>
#include <windows.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {
std::wstring Environment(const wchar_t* name) {
    wchar_t value[MAX_PATH]{};
    const DWORD size = GetEnvironmentVariableW(name, value, MAX_PATH);
    return size > 0 && size < MAX_PATH ? std::wstring(value, size)
                                       : std::wstring();
}

enum class Apartment { none, sta, mta };

struct Case {
    const char* name;
    char api;
    UINT function;
    FILEOP_FLAGS flags;
    std::wstring from; // NUL-separated, double NUL added on use.
    std::wstring to;
    Apartment apartment = Apartment::none;
    std::wstring directory; // Current directory during the call, if any.
    int result = -1;
    BOOL aborted = -1;
    volatile LONG returned = 0;
};

std::string Ansi(const std::wstring& text) {
    if (text.empty()) {
        return std::string(2, '\0');
    }
    const int size =
        WideCharToMultiByte(CP_ACP, 0, text.c_str(), int(text.size()), nullptr,
                            0, nullptr, nullptr);
    std::string result(size_t(size), '\0');
    WideCharToMultiByte(CP_ACP, 0, text.c_str(), int(text.size()),
                        result.data(), size, nullptr, nullptr);
    result.append(2, '\0');
    return result;
}

DWORD WINAPI Run(void* parameter) {
    auto& item = *static_cast<Case*>(parameter);
    bool com = false;
    if (item.apartment != Apartment::none) {
        com = SUCCEEDED(CoInitializeEx(nullptr, item.apartment == Apartment::sta
                                                    ? COINIT_APARTMENTTHREADED
                                                    : COINIT_MULTITHREADED));
    }
    if (item.api == 'A') {
        const std::string from = Ansi(item.from);
        const std::string to = Ansi(item.to);
        SHFILEOPSTRUCTA operation{};
        operation.wFunc = item.function;
        operation.pFrom = from.c_str();
        operation.pTo = item.to.empty() ? nullptr : to.c_str();
        operation.fFlags = item.flags;
        operation.fAnyOperationsAborted = TRUE;
        item.result = SHFileOperationA(&operation);
        item.aborted = operation.fAnyOperationsAborted;
    } else {
        const std::wstring from = item.from + std::wstring(2, L'\0');
        const std::wstring to = item.to + std::wstring(2, L'\0');
        SHFILEOPSTRUCTW operation{};
        operation.wFunc = item.function;
        operation.pFrom = from.c_str();
        operation.pTo = item.to.empty() ? nullptr : to.c_str();
        operation.fFlags = item.flags;
        operation.fAnyOperationsAborted = TRUE;
        item.result = SHFileOperationW(&operation);
        item.aborted = operation.fAnyOperationsAborted;
    }
    InterlockedExchange(&item.returned, 1);
    if (com) {
        CoUninitialize();
    }
    return 0;
}

// Returns false when the call did not return within ten seconds.
bool Call(Case& item) {
    wchar_t previous[MAX_PATH]{};
    if (!item.directory.empty()) {
        GetCurrentDirectoryW(MAX_PATH, previous);
        SetCurrentDirectoryW(item.directory.c_str());
    }
    const HANDLE thread = CreateThread(nullptr, 0, Run, &item, 0, nullptr);
    const bool finished =
        thread != nullptr && WaitForSingleObject(thread, 10000) == 0;
    if (thread != nullptr) {
        CloseHandle(thread);
    }
    if (!item.directory.empty()) {
        SetCurrentDirectoryW(previous);
    }
    std::printf("case=%s returned=%d result=%d aborted=%d\n", item.name,
                InterlockedCompareExchange(&item.returned, 0, 0) ? 1 : 0,
                item.result, static_cast<int>(item.aborted));
    std::fflush(stdout);
    return finished;
}
} // namespace

int main() {
    // A static import, so that the observation DLL loads before main.
    volatile auto factory = &DirectInput8Create;
    // The main thread is the main STA, so that an STA worker reports "sta".
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::wstring documents = Environment(L"TDUR_PRIVATE_DOCUMENTS");
    const std::wstring outside = Environment(L"TDUR_SHELL_DELETE_OUTSIDE");
    const std::wstring shortName = Environment(L"TDUR_SHELL_DELETE_SHORT");
    if (documents.empty() || outside.empty() || factory == nullptr ||
        FAILED(com)) {
        std::printf("witness configuration missing\n");
        return 2;
    }
    const std::wstring root = documents + L"\\Test Drive Unlimited\\savegame";
    const auto at = [&](const wchar_t* leaf) { return root + L"\\" + leaf; };
    const auto out = [&](const wchar_t* leaf) {
        return outside + L"\\" + leaf;
    };
    constexpr FILEOP_FLAGS game = 0x0414;
    std::vector<Case> handled;
    handled.push_back({"a-profile", 'A', FO_DELETE, game, at(L"ProfileA")});
    handled.push_back({"w-profile", 'W', FO_DELETE, game, at(L"ProfileW")});
    Case relative{"relative", 'A', FO_DELETE, game, L"ProfileRel"};
    relative.directory = root;
    handled.push_back(relative);
    handled.push_back(
        {"dotdot", 'W', FO_DELETE, game, at(L"ProfileA2\\..\\ProfileDot")});
    if (!shortName.empty()) {
        handled.push_back({"short", 'A', FO_DELETE, game, shortName});
    } else {
        std::printf("case=short skipped=no-short-names\n");
    }
    handled.push_back({"junction", 'A', FO_DELETE, game, at(L"ProfileJ")});
    handled.push_back({"readonly", 'W', FO_DELETE, game, at(L"ProfileRO")});
    handled.push_back({"missing", 'A', FO_DELETE, game, at(L"NoSuchProfile")});
    Case sta{"sta", 'W', FO_DELETE, game, at(L"ProfileSTA")};
    sta.apartment = Apartment::sta;
    handled.push_back(sta);
    Case mta{"mta", 'A', FO_DELETE, game, at(L"ProfileMTA")};
    mta.apartment = Apartment::mta;
    handled.push_back(mta);
    bool hung = false;
    for (auto& item : handled) {
        if (!Call(item)) {
            hung = true;
            break;
        }
    }
    // The launcher checks the disk before the forwarded cases: the last one
    // lets the real Shell delete the whole synthetic save root.
    std::printf("handled-done\n");
    std::fflush(stdout);
    if (!hung) {
        char line[16]{};
        if (std::fgets(line, sizeof(line), stdin) == nullptr) {
            hung = true;
        }
    }
    // Forwarded requests reach the real Shell: last, on absent paths only
    // (the recycle bin is never touched), then the root itself.
    std::vector<Case> forwarded;
    if (!hung) {
        forwarded.push_back(
            {"outside", 'A', FO_DELETE, game, out(L"NoSuchDir")});
        forwarded.push_back({"mixed", 'W', FO_DELETE, game,
                             at(L"NoSuchA") + L'\0' + out(L"NoSuchB")});
        forwarded.push_back(
            {"wildcard", 'A', FO_DELETE, game, at(L"TDURNOMATCH*.none")});
        forwarded.push_back(
            {"allowundo", 'W', FO_DELETE, 0x0454, at(L"NoSuchUndo")});
        forwarded.push_back({"copy", 'A', FO_COPY, game, at(L"NoSuchCopy"),
                             out(L"NoSuchCopyDest")});
        // A named stream under the root, on a file that does not exist.
        forwarded.push_back(
            {"stream", 'W', FO_DELETE, game, at(L"NoSuchStream.sav:flux")});
        // The root is not strictly under itself: forwarded.
        forwarded.push_back({"root", 'W', FO_DELETE, game, root});
    }
    for (auto& item : forwarded) {
        if (!Call(item)) {
            hung = true;
        }
    }
    std::printf("witness-end hung=%d\n", hung ? 1 : 0);
    std::fflush(stdout);
    if (hung) {
        TerminateProcess(GetCurrentProcess(), 0);
    }
    CoUninitialize();
    return 0;
}
