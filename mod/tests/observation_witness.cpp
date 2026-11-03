#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windows.h>
#include <winioctl.h>
#include <winternl.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <thread>
#include <vector>

namespace {
std::atomic<unsigned> failures = 0;
std::atomic<unsigned> privateWrites = 0;

std::array<wchar_t, MAX_PATH> Environment(const wchar_t* name) {
    std::array<wchar_t, MAX_PATH> value{};
    const auto size = GetEnvironmentVariableW(name, value.data(), MAX_PATH);
    if (size == 0 || size >= MAX_PATH) {
        ++failures;
        value[0] = L'\0';
    }
    return value;
}

bool IsPrivate(const wchar_t* path) {
    const auto expected = Environment(L"TDUR_PRIVATE_DOCUMENTS");
    return path && expected[0] && _wcsicmp(path, expected.data()) == 0;
}

void PrivateMarker(const wchar_t* folder, const wchar_t* name) {
    if (!IsPrivate(folder)) {
        ++failures;
        return;
    }
    std::array<wchar_t, MAX_PATH> path{};
    const int size =
        swprintf_s(path.data(), path.size(), L"%s\\%s-%lu", folder, name,
                   static_cast<unsigned long>(GetCurrentThreadId()));
    if (size <= 0) {
        ++failures;
        return;
    }
    const HANDLE file =
        CreateFileW(path.data(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        ++failures;
        return;
    }
    constexpr char content[] =
        "TDU-Recompilation controlled observation fixture\n";
    DWORD written = 0;
    if (!WriteFile(file, content, DWORD(sizeof(content) - 1), &written,
                   nullptr) ||
        written != sizeof(content) - 1) {
        ++failures;
    } else {
        ++privateWrites;
    }
    CloseHandle(file);
}

void QueryFolders(bool global) {
    std::array<char, MAX_PATH> ansi{};
    std::array<wchar_t, MAX_PATH> wide{};
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL | CSIDL_FLAG_CREATE,
                                nullptr, SHGFP_TYPE_CURRENT, ansi.data())) ||
        MultiByteToWideChar(CP_ACP, 0, ansi.data(), -1, wide.data(),
                            MAX_PATH) == 0 ||
        !IsPrivate(wide.data())) {
        ++failures;
    } else {
        PrivateMarker(wide.data(),
                      global ? L"global-import-a.txt" : L"main-import-a.txt");
    }
    using FolderA = decltype(&SHGetFolderPathA);
    const auto symbol =
        GetProcAddress(GetModuleHandleW(L"shell32.dll"), "SHGetFolderPathA");
    if (symbol == nullptr) {
        ++failures;
        return;
    }
    const auto dynamic = std::bit_cast<FolderA>(symbol);
    if (FAILED(dynamic(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT,
                       ansi.data())) ||
        MultiByteToWideChar(CP_ACP, 0, ansi.data(), -1, wide.data(),
                            MAX_PATH) == 0 ||
        !IsPrivate(wide.data())) {
        ++failures;
    } else {
        PrivateMarker(wide.data(),
                      global ? L"global-dynamic-a.txt" : L"main-dynamic-a.txt");
    }
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr,
                                SHGFP_TYPE_CURRENT, wide.data())) ||
        !IsPrivate(wide.data())) {
        ++failures;
    } else {
        PrivateMarker(wide.data(),
                      global ? L"global-import-w.txt" : L"main-import-w.txt");
    }
    PWSTR known = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &known)) ||
        !IsPrivate(known)) {
        ++failures;
    } else {
        PrivateMarker(known, global ? L"global-known.txt" : L"main-known.txt");
    }
    CoTaskMemFree(known);
}

struct BeforeExecutableEntryCompletes {
    BeforeExecutableEntryCompletes() { QueryFolders(true); }
} beforeMain;

void CheckInputFactory() {
    IDirectInput8W* input = nullptr;
    const HRESULT proxied = DirectInput8Create(
        GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
        reinterpret_cast<void**>(&input), nullptr);
    if (FAILED(proxied) || input == nullptr) {
        ++failures;
    }
    if (input) {
        input->Release();
    }
    std::array<wchar_t, MAX_PATH> system{};
    const auto length = GetSystemDirectoryW(system.data(), MAX_PATH);
    constexpr wchar_t name[] = L"\\dinput8.dll";
    if (length == 0 || length + std::size(name) >= system.size()) {
        ++failures;
        return;
    }
    std::memcpy(system.data() + length, name, sizeof(name));
    const auto realModule =
        LoadLibraryExW(system.data(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!realModule) {
        ++failures;
        return;
    }
    const auto symbol = GetProcAddress(realModule, "DirectInput8Create");
    if (!symbol) {
        ++failures;
        FreeLibrary(realModule);
        return;
    }
    const auto real = std::bit_cast<decltype(&DirectInput8Create)>(symbol);
    input = nullptr;
    const HRESULT direct =
        real(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
             reinterpret_cast<void**>(&input), nullptr);
    if (direct != proxied || input == nullptr) {
        ++failures;
    }
    if (input) {
        input->Release();
    }
    void* invalidA = nullptr;
    void* invalidB = nullptr;
    const auto invalidProxy = DirectInput8Create(
        GetModuleHandleW(nullptr), 0, IID_IDirectInput8W, &invalidA, nullptr);
    const auto invalidReal = real(GetModuleHandleW(nullptr), 0,
                                  IID_IDirectInput8W, &invalidB, nullptr);
    if (invalidProxy != invalidReal || SUCCEEDED(invalidProxy) || invalidA ||
        invalidB) {
        ++failures;
    }
    FreeLibrary(realModule);
}

void CheckSourceRefusal() {
    const auto source = Environment(L"TDUR_SOURCE_DOCUMENTS");
    std::array<wchar_t, MAX_PATH> path{};
    for (const auto* name : {L"save0.sav", L"forbidden-new.sav"}) {
        swprintf_s(path.data(), path.size(), L"%s\\%s", source.data(), name);
        const HANDLE file =
            CreateFileW(path.data(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            ++failures;
            CloseHandle(file);
        } else if (GetLastError() != ERROR_ACCESS_DENIED &&
                   GetLastError() != ERROR_SHARING_VIOLATION) {
            ++failures;
        }
    }
    for (const auto* pattern :
         {L"\\\\?\\%s\\forbidden-new.sav",
          L"%s\\..\\source-documents\\forbidden-new.sav"}) {
        swprintf_s(path.data(), path.size(), pattern, source.data());
        const HANDLE file =
            CreateFileW(path.data(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            ++failures;
            CloseHandle(file);
        } else if (GetLastError() != ERROR_ACCESS_DENIED) {
            ++failures;
        }
    }
    using NativeCreate = NTSTATUS(NTAPI*)(
        PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
        PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
    const auto address =
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile");
    const HANDLE directory = CreateFileW(
        source.data(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (!address || directory == INVALID_HANDLE_VALUE) {
        ++failures;
        return;
    }
    const auto native = std::bit_cast<NativeCreate>(address);
    wchar_t leaf[] = L"forbidden-new.sav";
    UNICODE_STRING name{USHORT(sizeof(leaf) - sizeof(wchar_t)),
                        USHORT(sizeof(leaf)), leaf};
    OBJECT_ATTRIBUTES attributes{
        sizeof(OBJECT_ATTRIBUTES), directory, &name, 0x40, nullptr, nullptr};
    IO_STATUS_BLOCK status{};
    HANDLE file = nullptr;
    const auto result =
        native(&file, GENERIC_WRITE, &attributes, &status, nullptr,
               FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, 5, 0x40, nullptr, 0);
    if (result >= 0 || file != nullptr) {
        ++failures;
        if (file) {
            CloseHandle(file);
        }
    } else if (result != static_cast<NTSTATUS>(0xC0000022UL)) {
        ++failures;
    }
    CloseHandle(directory);
}

void CheckInstallationRefusal() {
    std::array<wchar_t, MAX_PATH> installation{};
    if (GetEnvironmentVariableW(L"TDUR_SOURCE_INSTALLATION",
                                installation.data(), MAX_PATH) == 0) {
        return;
    }
    std::array<wchar_t, MAX_PATH> path{};
    for (const auto* name :
         {L"fixture-resource.bin", L"forbidden-installation-new.bin"}) {
        swprintf_s(path.data(), path.size(), L"%s\\%s", installation.data(),
                   name);
        const HANDLE file =
            CreateFileW(path.data(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            ++failures;
            CloseHandle(file);
        } else if (GetLastError() != ERROR_ACCESS_DENIED) {
            ++failures;
        }
    }
}

void CheckFrequentFolderCalls() {
    std::array<char, MAX_PATH> path{};
    using NativeCreate = NTSTATUS(NTAPI*)(
        PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
        PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
    const auto symbol =
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile");
    if (!symbol) {
        ++failures;
        return;
    }
    const auto native = std::bit_cast<NativeCreate>(symbol);
    wchar_t empty[] = L"";
    UNICODE_STRING name{0, sizeof(wchar_t), empty};
    OBJECT_ATTRIBUTES attributes{
        sizeof(OBJECT_ATTRIBUTES), nullptr, &name, 0, nullptr, nullptr};
    const auto emptyCall = [&] {
        IO_STATUS_BLOCK status{};
        HANDLE handle = nullptr;
        const auto result =
            native(&handle, GENERIC_WRITE, &attributes, &status, nullptr,
                   FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, 1, 0, nullptr, 0);
        if (result != static_cast<NTSTATUS>(0xC0000022UL) || handle) {
            ++failures;
        }
    };
    for (unsigned i = 0; i < 100000; ++i) {
        if (FAILED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr,
                                    SHGFP_TYPE_CURRENT, path.data()))) {
            ++failures;
        }
        emptyCall();
    }
    // Exercise the real five-second aggregation period, with no disk spam.
    Sleep(5100);
    if (FAILED(SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr,
                                SHGFP_TYPE_CURRENT, path.data()))) {
        ++failures;
    }
    emptyCall();
    std::array<wchar_t, MAX_PATH> wide{};
    if (MultiByteToWideChar(CP_ACP, 0, path.data(), -1, wide.data(),
                            MAX_PATH) == 0 ||
        !IsPrivate(wide.data())) {
        ++failures;
    }
}

void CheckMissingParentErrors() {
    const auto folder = Environment(L"TDUR_PRIVATE_DOCUMENTS");
    std::array<wchar_t, MAX_PATH> path{};
    const auto check = [&](const wchar_t* leaf, DWORD disposition,
                           DWORD expected) {
        if (swprintf_s(path.data(), path.size(), L"%s\\%s", folder.data(),
                       leaf) <= 0) {
            ++failures;
            return;
        }
        const auto file =
            CreateFileW(path.data(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                        disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
        const auto error = GetLastError();
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
            ++failures;
        } else if (error != expected) {
            std::cerr << "Missing-path error: expected=" << expected
                      << " actual=" << error << '\n';
            ++failures;
        }
    };
    check(L"absent-leaf.dat", OPEN_EXISTING, ERROR_FILE_NOT_FOUND);
    check(L"future\\nested\\profile.dat", OPEN_EXISTING, ERROR_PATH_NOT_FOUND);
    check(L"future\\nested\\profile.dat", CREATE_NEW, ERROR_PATH_NOT_FOUND);
}

void CheckCurrentProcessFolderToken() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_IMPERSONATE,
                          &token)) {
        ++failures;
        return;
    }
    std::array<char, MAX_PATH> ansi{};
    std::array<wchar_t, MAX_PATH> wide{};
    bool redirected = true;
    if (SHGetFolderPathA(nullptr, CSIDL_PERSONAL, token, SHGFP_TYPE_CURRENT,
                         ansi.data()) != S_OK ||
        !MultiByteToWideChar(CP_ACP, 0, ansi.data(), -1, wide.data(),
                             MAX_PATH) ||
        !IsPrivate(wide.data())) {
        ++failures;
        redirected = false;
    }
    if (SHGetFolderPathW(nullptr, CSIDL_PERSONAL, token, SHGFP_TYPE_CURRENT,
                         wide.data()) != S_OK ||
        !IsPrivate(wide.data())) {
        ++failures;
        redirected = false;
    }
    PWSTR known = nullptr;
    if (SHGetKnownFolderPath(FOLDERID_Documents, 0, token, &known) != S_OK ||
        !IsPrivate(known)) {
        ++failures;
        redirected = false;
    }
    CoTaskMemFree(known);
    CloseHandle(token);
    // D3: every refusal of the three Documents APIs is observable;
    // run_fixture.py requires one event per API and reason.
    for (const auto refused :
         {INVALID_HANDLE_VALUE, reinterpret_cast<HANDLE>(INT_PTR(1))}) {
        ansi.fill('x');
        if (SHGetFolderPathA(nullptr, CSIDL_PERSONAL, refused,
                             SHGFP_TYPE_CURRENT,
                             ansi.data()) != E_ACCESSDENIED ||
            ansi[0] != '\0')
            ++failures;
        wide.fill(L'x');
        if (SHGetFolderPathW(nullptr, CSIDL_PERSONAL, refused,
                             SHGFP_TYPE_CURRENT,
                             wide.data()) != E_ACCESSDENIED ||
            wide[0] != L'\0')
            ++failures;
        PWSTR refusedKnown = nullptr;
        if (SHGetKnownFolderPath(FOLDERID_Documents, 0, refused,
                                 &refusedKnown) != E_ACCESSDENIED ||
            refusedKnown != nullptr)
            ++failures;
        CoTaskMemFree(refusedKnown);
    }
    // Null outputs only once the hooks are known active: the real APIs need a
    // valid output.
    if (redirected) {
        if (SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr,
                             SHGFP_TYPE_CURRENT, nullptr) != E_POINTER)
            ++failures;
        if (SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr,
                             SHGFP_TYPE_CURRENT, nullptr) != E_POINTER)
            ++failures;
        if (SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, nullptr) !=
            E_POINTER)
            ++failures;
    }
}

// --- NT name forms (D1) and trailing separators (D2) ----------------------
// Invented TDUR_WITNESS_* names only, inside this run's private documents.
using NativeCreateFn = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK,
                                        POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
                                        PLARGE_INTEGER, ULONG, ULONG, ULONG,
                                        ULONG, PVOID, ULONG);
constexpr ULONG DispositionOpen = 1;
constexpr ULONG DispositionCreate = 2;
constexpr ULONG DispositionOpenIf = 3;
constexpr ULONG OptionDirectory = 0x1;
constexpr ULONG OptionNonDirectory = 0x40;
constexpr ULONG OptionSynchronous = 0x20;
constexpr NTSTATUS AccessDenied = static_cast<NTSTATUS>(0xC0000022UL);
constexpr NTSTATUS PathSyntaxBad = static_cast<NTSTATUS>(0xC000003BUL);
constexpr NTSTATUS InvalidParameter = static_cast<NTSTATUS>(0xC000000DUL);
constexpr NTSTATUS NotADirectory = static_cast<NTSTATUS>(0xC0000103UL);

void Expect(bool ok, const char* label, unsigned long code = 0) {
    if (!ok) {
        ++failures;
        std::cerr << "form case failed: " << label << " code=0x" << std::hex
                  << code << std::dec << '\n';
    }
}

unsigned long Code(NTSTATUS status) {
    return static_cast<unsigned long>(status);
}

bool Exists(const wchar_t* path) {
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

bool IsDirectory(const wchar_t* path) {
    const DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::array<wchar_t, MAX_PATH> Join(const wchar_t* folder, const wchar_t* leaf) {
    std::array<wchar_t, MAX_PATH> path{};
    if (swprintf_s(path.data(), path.size(), L"%s\\%s", folder, leaf) <= 0) {
        ++failures;
        path[0] = L'\0';
    }
    return path;
}

// NtCreateFile on one object name, optionally relative to a root handle.
NTSTATUS NativeOpen(const wchar_t* objectName, ACCESS_MASK access,
                    ULONG disposition, ULONG options, bool& opened,
                    HANDLE root = nullptr) {
    opened = false;
    const auto symbol =
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile");
    std::array<wchar_t, 1024> buffer{};
    if (symbol == nullptr ||
        wcscpy_s(buffer.data(), buffer.size(), objectName) != 0) {
        ++failures;
        return AccessDenied;
    }
    const auto native = std::bit_cast<NativeCreateFn>(symbol);
    UNICODE_STRING name{
        static_cast<USHORT>(std::wcslen(buffer.data()) * sizeof(wchar_t)),
        static_cast<USHORT>(buffer.size() * sizeof(wchar_t)), buffer.data()};
    OBJECT_ATTRIBUTES attributes{
        sizeof(OBJECT_ATTRIBUTES), root, &name, 0x40, nullptr, nullptr};
    IO_STATUS_BLOCK status{};
    HANDLE handle = nullptr;
    const NTSTATUS result =
        native(&handle, access | SYNCHRONIZE, &attributes, &status, nullptr,
               FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ | FILE_SHARE_WRITE,
               disposition, options | OptionSynchronous, nullptr, 0);
    if (result >= 0 && handle != nullptr) {
        opened = true;
        CloseHandle(handle);
    }
    return result;
}

// D1: a DOS device or bare NT name never becomes a file of the current
// directory. Confinement refusals return access denied; the two NT syntax
// forms return the status the system returns (measured locally).
void CheckNtNameForms() {
    const auto folder = Environment(L"TDUR_PRIVATE_DOCUMENTS");
    const auto cwd = Join(folder.data(), L"TDUR_WITNESS_CWD");
    std::array<wchar_t, MAX_PATH> saved{};
    if (!GetCurrentDirectoryW(MAX_PATH, saved.data()) ||
        !CreateDirectoryW(cwd.data(), nullptr) ||
        !SetCurrentDirectoryW(cwd.data())) {
        Expect(false, "D1 private current directory", GetLastError());
        return;
    }
    const auto here = [&](const wchar_t* leaf) {
        return Exists(Join(cwd.data(), leaf).data());
    };
    bool opened = false;
    auto status = NativeOpen(L"\\??\\TDUR_WITNESS_ABSENT_DEVICE", GENERIC_WRITE,
                             DispositionOpenIf, OptionNonDirectory, opened);
    Expect(status == AccessDenied && !opened &&
               !here(L"TDUR_WITNESS_ABSENT_DEVICE"),
           "D1 invented absent device", Code(status));
    const HANDLE collision = CreateFileW(
        Join(cwd.data(), L"TDUR_WITNESS_COLLISION").data(), GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    Expect(collision != INVALID_HANDLE_VALUE, "D1 collision fixture file",
           GetLastError());
    if (collision != INVALID_HANDLE_VALUE)
        CloseHandle(collision);
    status = NativeOpen(L"\\??\\TDUR_WITNESS_COLLISION", GENERIC_WRITE,
                        DispositionOpen, OptionNonDirectory, opened);
    Expect(status == AccessDenied && !opened,
           "D1 device name equal to a current-directory file", Code(status));
    for (const auto* device : {L"\\??\\Global\\TDUR_WITNESS_ABSENT_DEVICE",
                               L"\\??\\pipe\\TDUR_WITNESS_ABSENT_PIPE"}) {
        status = NativeOpen(device, GENERIC_WRITE, DispositionOpenIf,
                            OptionNonDirectory, opened);
        Expect(status == AccessDenied && !opened,
               "D1 Global or pipe device name", Code(status));
    }
    Expect(!here(L"Global") && !here(L"pipe"),
           "D1 no device namespace folder in the current directory");
    status = NativeOpen(L"TDUR_WITNESS_RELATIVE", GENERIC_WRITE,
                        DispositionOpenIf, OptionNonDirectory, opened);
    Expect(status == PathSyntaxBad && !opened &&
               !here(L"TDUR_WITNESS_RELATIVE"),
           "D1 bare name without root", Code(status));
    const std::array<wchar_t, 7> volume{L'\\',  L'?', L'?', L'\\',
                                        cwd[0], L':', L'\0'};
    status = NativeOpen(volume.data(), FILE_WRITE_ATTRIBUTES, DispositionOpen,
                        0, opened);
    Expect(status == AccessDenied && !opened, "D1 drive volume name",
           Code(status));
    const HANDLE root = CreateFileW(
        cwd.data(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    std::array<wchar_t, MAX_PATH + 8> rooted{};
    const bool rootedText =
        swprintf_s(rooted.data(), rooted.size(),
                   L"\\??\\%s\\TDUR_WITNESS_ROOTED", cwd.data()) > 0;
    Expect(root != INVALID_HANDLE_VALUE && rootedText, "D1 rooted fixture",
           GetLastError());
    if (root != INVALID_HANDLE_VALUE) {
        if (rootedText) {
            status = NativeOpen(rooted.data(), GENERIC_WRITE, DispositionOpenIf,
                                OptionNonDirectory, opened, root);
            Expect(status == InvalidParameter && !opened &&
                       !here(L"TDUR_WITNESS_ROOTED"),
                   "D1 absolute name with a root handle", Code(status));
        }
        CloseHandle(root);
    }
    std::array<wchar_t, MAX_PATH + 8> drive{};
    if (swprintf_s(drive.data(), drive.size(),
                   L"\\??\\%s\\TDUR_WITNESS_DRIVE_FILE", cwd.data()) > 0) {
        status = NativeOpen(drive.data(), GENERIC_WRITE, DispositionCreate,
                            OptionNonDirectory, opened);
        Expect(status >= 0 && opened && here(L"TDUR_WITNESS_DRIVE_FILE"),
               "D1 real drive path still opens", Code(status));
    } else {
        Expect(false, "D1 drive path text");
    }
    Expect(SetCurrentDirectoryW(saved.data()) != FALSE,
           "D1 restore current directory", GetLastError());
}

// Directory junction (mount point) to a private target, made by the witness
// itself; layout of REPARSE_DATA_BUFFER for a mount point.
struct MountPoint {
    DWORD tag;
    WORD dataLength;
    WORD reserved;
    WORD substituteOffset;
    WORD substituteLength;
    WORD printOffset;
    WORD printLength;
    wchar_t path[MAX_PATH + 16];
};

bool MakeJunction(const wchar_t* link, const wchar_t* target) {
    if (!CreateDirectoryW(link, nullptr))
        return false;
    const HANDLE directory = CreateFileW(
        link, GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (directory == INVALID_HANDLE_VALUE)
        return false;
    MountPoint buffer{};
    const int length =
        swprintf_s(buffer.path, std::size(buffer.path), L"\\??\\%s", target);
    bool made = false;
    if (length > 0) {
        // Substitute name, NUL, empty print name, NUL.
        const auto bytes = static_cast<WORD>(length * sizeof(wchar_t));
        buffer.tag = IO_REPARSE_TAG_MOUNT_POINT;
        buffer.substituteLength = bytes;
        buffer.printOffset = static_cast<WORD>(bytes + sizeof(wchar_t));
        buffer.dataLength =
            static_cast<WORD>(4 * sizeof(WORD) + bytes + 2 * sizeof(wchar_t));
        DWORD returned = 0;
        made = DeviceIoControl(directory, FSCTL_SET_REPARSE_POINT, &buffer,
                               8u + buffer.dataLength, nullptr, 0, &returned,
                               nullptr) != FALSE;
    }
    CloseHandle(directory);
    return made;
}

// D2: a trailing separator names the leaf itself; the system keeps the last
// word on file versus directory semantics. Source and reparse rules hold.
void CheckTrailingSeparators() {
    const auto folder = Environment(L"TDUR_PRIVATE_DOCUMENTS");
    const auto source = Environment(L"TDUR_SOURCE_DOCUMENTS");
    const auto win32 = Join(folder.data(), L"TDUR_WITNESS_DIR_WIN32\\");
    const BOOL madeWin32 = CreateDirectoryW(win32.data(), nullptr);
    const DWORD win32Error = madeWin32 ? ERROR_SUCCESS : GetLastError();
    const auto win32Leaf = Join(folder.data(), L"TDUR_WITNESS_DIR_WIN32");
    Expect(madeWin32 != FALSE && IsDirectory(win32Leaf.data()),
           "D2 Win32 directory with a trailing separator", win32Error);
    std::array<wchar_t, MAX_PATH + 8> nt{};
    bool opened = false;
    NTSTATUS status = AccessDenied;
    if (swprintf_s(nt.data(), nt.size(), L"\\??\\%s\\TDUR_WITNESS_DIR_NT\\",
                   folder.data()) > 0)
        status = NativeOpen(nt.data(), FILE_LIST_DIRECTORY, DispositionCreate,
                            OptionDirectory, opened);
    Expect(status >= 0 && opened &&
               IsDirectory(Join(folder.data(), L"TDUR_WITNESS_DIR_NT").data()),
           "D2 native directory request created as a directory", Code(status));
    const auto absent = Join(folder.data(), L"TDUR_WITNESS_ABSENT\\leaf\\");
    const BOOL madeAbsent = CreateDirectoryW(absent.data(), nullptr);
    const DWORD absentError = madeAbsent ? ERROR_SUCCESS : GetLastError();
    Expect(!madeAbsent && absentError == ERROR_PATH_NOT_FOUND &&
               !Exists(Join(folder.data(), L"TDUR_WITNESS_ABSENT").data()),
           "D2 missing intermediate parent keeps ERROR_PATH_NOT_FOUND",
           absentError);
    const HANDLE file = CreateFileW(
        Join(folder.data(), L"TDUR_WITNESS_FILE").data(), GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    Expect(file != INVALID_HANDLE_VALUE, "D2 file fixture", GetLastError());
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    status = AccessDenied;
    if (swprintf_s(nt.data(), nt.size(), L"\\??\\%s\\TDUR_WITNESS_FILE\\",
                   folder.data()) > 0)
        status = NativeOpen(nt.data(), GENERIC_WRITE, DispositionOpen,
                            OptionNonDirectory, opened);
    Expect(status == NotADirectory && !opened,
           "D2 file named with a trailing separator is not opened",
           Code(status));
    const auto guarded = Join(source.data(), L"TDUR_WITNESS_SOURCE_DIR\\");
    const BOOL madeSource = CreateDirectoryW(guarded.data(), nullptr);
    const DWORD sourceError = madeSource ? ERROR_SUCCESS : GetLastError();
    Expect(!madeSource && sourceError == ERROR_ACCESS_DENIED &&
               !Exists(Join(source.data(), L"TDUR_WITNESS_SOURCE_DIR").data()),
           "D2 source root stays protected with a trailing separator",
           sourceError);
    const auto target = Join(folder.data(), L"TDUR_WITNESS_TARGET");
    const auto link = Join(folder.data(), L"TDUR_WITNESS_JUNCTION");
    const bool junction = CreateDirectoryW(target.data(), nullptr) != FALSE &&
                          MakeJunction(link.data(), target.data());
    Expect(junction, "D2 junction fixture", GetLastError());
    if (junction) {
        const auto through = Join(link.data(), L"TDUR_WITNESS_THROUGH\\");
        const BOOL madeThrough = CreateDirectoryW(through.data(), nullptr);
        const DWORD throughError = madeThrough ? ERROR_SUCCESS : GetLastError();
        Expect(!madeThrough && throughError == ERROR_ACCESS_DENIED &&
                   !Exists(Join(target.data(), L"TDUR_WITNESS_THROUGH").data()),
               "D2 reparse point stays refused", throughError);
    }
}
} // namespace

int main() {
    const auto jobName = Environment(L"TDUR_OBSERVATION_JOB");
    const HANDLE job = OpenJobObjectW(JOB_OBJECT_QUERY, FALSE, jobName.data());
    BOOL inJob = FALSE;
    if (job == nullptr || !IsProcessInJob(GetCurrentProcess(), job, &inJob) ||
        !inJob) {
        ++failures;
    }
    if (job != nullptr) {
        CloseHandle(job);
    }
    QueryFolders(false);
    CheckInputFactory();
    CheckSourceRefusal();
    CheckInstallationRefusal();
    CheckMissingParentErrors();
    CheckNtNameForms();
    CheckTrailingSeparators();
    CheckCurrentProcessFolderToken();
    CheckFrequentFolderCalls();
    const auto refused = ShellExecuteW(nullptr, L"open", L"cmd.exe", L"/c exit",
                                       nullptr, SW_HIDE);
    if (reinterpret_cast<INT_PTR>(refused) != SE_ERR_ACCESSDENIED ||
        GetLastError() != ERROR_ACCESS_DENIED) {
        ++failures;
    }
    std::vector<std::thread> workers;
    for (unsigned index = 0; index < 4; ++index) {
        workers.emplace_back([] { QueryFolders(false); });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    std::array<wchar_t, MAX_PATH> executable{};
    if (GetModuleFileNameW(nullptr, executable.data(), MAX_PATH) == 0) {
        ++failures;
    }
    std::array<wchar_t, MAX_PATH + 32> command{};
    swprintf_s(command.data(), command.size(), L"\"%s\" --child",
               executable.data());
    STARTUPINFOW startup{sizeof(STARTUPINFOW)};
    PROCESS_INFORMATION child{};
    if (CreateProcessW(executable.data(), command.data(), nullptr, nullptr,
                       FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                       &child)) {
        ++failures;
        TerminateProcess(child.hProcess, 1);
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
    } else if (GetLastError() != ERROR_ACCESS_DENIED || child.hProcess ||
               child.hThread) {
        ++failures;
    }
    std::cout << "{\"project\":\"TDU-Recompilation\",\"fixture\":\"pre-entry-"
                 "x86-observer\","
                 "\"game_executed\":false,\"private_writes\":"
              << privateWrites.load() << ",\"failures\":" << failures.load()
              << "}\n";
    return failures == 0 && privateWrites >= 24 ? 0 : 1;
}
