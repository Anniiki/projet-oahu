#include "tdur/mod/observation_hooks.hpp"
#include "tdur/mod/native_input_path.hpp"
#include "tdur/mod/native_service_path.hpp"

#include <detours.h>
#include <intrin.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winternl.h>

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {
constexpr size_t Capacity = 1024;
using NativeCreateFile = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK,
                                          POBJECT_ATTRIBUTES, PIO_STATUS_BLOCK,
                                          PLARGE_INTEGER, ULONG, ULONG, ULONG,
                                          ULONG, PVOID, ULONG);
auto realFolderA = &SHGetFolderPathA;
auto realFolderW = &SHGetFolderPathW;
auto realKnownFolder = &SHGetKnownFolderPath;
auto realShellA = &ShellExecuteA;
auto realShellW = &ShellExecuteW;
auto realShellExA = &ShellExecuteExA;
auto realShellExW = &ShellExecuteExW;
auto realProcessA = &CreateProcessA;
auto realProcessW = &CreateProcessW;
auto realAsUserA = &CreateProcessAsUserA;
auto realAsUserW = &CreateProcessAsUserW;
auto realWithLogon = &CreateProcessWithLogonW;
auto realWithToken = &CreateProcessWithTokenW;
auto realWinExec = &WinExec;
NativeCreateFile realNativeCreateFile = nullptr;
std::array<wchar_t, Capacity> privateRoot{};
std::array<char, MAX_PATH> privateAnsi{};
std::array<wchar_t, Capacity> sourceRoot{};
std::array<wchar_t, Capacity> sourceInstallation{};
std::array<std::array<wchar_t, Capacity>, 26> nativeVolumes{};
HANDLE logFile = INVALID_HANDLE_VALUE;
SRWLOCK logLock = SRWLOCK_INIT;
thread_local bool internalOperation = false;

// Documents decisions (accepted and refused, D3) and NT name-form refusals (D1)
// are written one line per call up to this count per event name and process,
// then counted exactly in the periodic summaries, which keeps the flood
// witness log bounded.
constexpr uint64_t DecisionLines = 512;

struct FrequentEvent {
    const char* name;
    uint64_t individualLimit = 8;
    std::atomic<uint64_t> total{};
    std::atomic<uint64_t> nonzero{};
    std::atomic<uint64_t> emptyPaths{};
    std::atomic<uint64_t> reported{};
    std::atomic<uint64_t> lastTick{};
    std::atomic_flag summaryLock{};
};
constinit std::array<FrequentEvent, 12> frequentEvents{
    {{"native-input-open-forwarded"},
     {"file-write-observed"},
     {"folder-a-redirected", DecisionLines},
     {"folder-a-refused", DecisionLines},
     {"folder-w-redirected", DecisionLines},
     {"folder-w-refused", DecisionLines},
     {"known-folder-redirected", DecisionLines},
     {"known-folder-refused", DecisionLines},
     {"DirectInput8Create-forwarded"},
     {"unconfined-write-refused"},
     {"nt-name-form-refused", DecisionLines},
     {"native-service-open-forwarded"}}};

struct InternalCall {
    bool previous = internalOperation;
    InternalCall() { internalOperation = true; }
    ~InternalCall() { internalOperation = previous; }
};

bool Prefix(const wchar_t* path, const wchar_t* root) noexcept {
    const auto length = std::wcslen(root);
    return length > 0 && std::wcslen(path) >= length &&
           CompareStringOrdinal(path, int(length), root, int(length), TRUE) ==
               CSTR_EQUAL &&
           (path[length] == L'\0' || path[length] == L'\\');
}

void StripWin32Prefix(wchar_t* path) noexcept {
    if (std::wcsncmp(path, L"\\\\?\\", 4) == 0 ||
        std::wcsncmp(path, L"\\??\\", 4) == 0) {
        std::memmove(path, path + 4,
                     (std::wcslen(path + 4) + 1) * sizeof(wchar_t));
    }
}

bool CanonicalPath(const wchar_t* input, std::array<wchar_t, Capacity>& result,
                   bool* missing_parent = nullptr,
                   bool* trailing_separator = nullptr) noexcept {
    if (missing_parent)
        *missing_parent = false;
    if (trailing_separator)
        *trailing_separator = false;
    const DWORD length =
        GetFullPathNameW(input, DWORD(result.size()), result.data(), nullptr);
    if (length == 0 || length >= result.size()) {
        return false;
    }
    StripWin32Prefix(result.data());
    if (std::wcslen(result.data()) < 3 || result[1] != L':' ||
        result[2] != L'\\') {
        return false;
    }
    for (auto& character : result) {
        if (character == L'/') {
            character = L'\\';
        }
    }
    // A trailing separator names the leaf itself, never one of its parents:
    // only the components before the leaf must exist. The caller restores the
    // separator so the system keeps the last word on file versus directory
    // semantics (D2).
    auto used = std::wcslen(result.data());
    while (used > 3 && result[used - 1] == L'\\') {
        result[--used] = L'\0';
        if (trailing_separator)
            *trailing_separator = true;
    }
    for (size_t index = 3; index <= used; ++index) {
        if (result[index] != L'\\' && result[index] != L'\0') {
            continue;
        }
        const wchar_t saved = result[index];
        result[index] = L'\0';
        const DWORD attributes = GetFileAttributesW(result.data());
        const DWORD error = attributes == INVALID_FILE_ATTRIBUTES
                                ? GetLastError()
                                : ERROR_SUCCESS;
        result[index] = saved;
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            return false;
        }
        if (attributes == INVALID_FILE_ATTRIBUTES && index != used) {
            if (missing_parent && (error == ERROR_FILE_NOT_FOUND ||
                                   error == ERROR_PATH_NOT_FOUND))
                *missing_parent = true;
            return false;
        }
    }
    std::array<wchar_t, Capacity> expanded{};
    const DWORD expandedLength = GetLongPathNameW(
        result.data(), expanded.data(), DWORD(expanded.size()));
    if (expandedLength > 0 && expandedLength < expanded.size()) {
        result = expanded;
    } else {
        auto* separator = std::wcsrchr(result.data(), L'\\');
        if (separator == nullptr) {
            return false;
        }
        const size_t offset = size_t(separator - result.data());
        std::array<wchar_t, Capacity> leaf{};
        std::wcscpy(leaf.data(), separator);
        result[offset] = L'\0';
        const DWORD parentLength = GetLongPathNameW(
            result.data(), expanded.data(), DWORD(expanded.size()));
        result[offset] = L'\\';
        if (parentLength == 0 ||
            parentLength + std::wcslen(leaf.data()) >= expanded.size()) {
            return false;
        }
        std::wcscat(expanded.data(), leaf.data());
        result = expanded;
    }
    auto finalLength = std::wcslen(result.data());
    while (finalLength > 3 && result[finalLength - 1] == L'\\') {
        result[--finalLength] = L'\0';
    }
    return true;
}

// Category of the NT object name, decided before any canonicalization
// (D1). Only file_path may reach CanonicalPath.
enum class NameForm {
    file_path,
    relative_without_root,
    absolute_with_root,
    dos_device,
    dos_unc,
    dos_volume,
    dos_drive_relative,
};

bool DriveLetter(wchar_t character) noexcept {
    return (character >= L'A' && character <= L'Z') ||
           (character >= L'a' && character <= L'z');
}

struct Refusal {
    DWORD error;
    NTSTATUS status;
};

// Missing parents and the two NT syntax forms reproduce the status the system
// returns for the same request (measured locally); every other refusal is
// confinement and returns access denied.
Refusal RefusalStatus(NameForm form, bool missing_parent) noexcept {
    if (missing_parent)
        return {ERROR_PATH_NOT_FOUND, static_cast<NTSTATUS>(0xC000003AUL)};
    if (form == NameForm::relative_without_root)
        return {ERROR_BAD_PATHNAME, static_cast<NTSTATUS>(0xC000003BUL)};
    if (form == NameForm::absolute_with_root)
        return {ERROR_INVALID_PARAMETER, static_cast<NTSTATUS>(0xC000000DUL)};
    return {ERROR_ACCESS_DENIED, static_cast<NTSTATUS>(0xC0000022UL)};
}

bool NativePath(const OBJECT_ATTRIBUTES& attributes,
                std::array<wchar_t, Capacity>& result, const char*& reason,
                NameForm& form) noexcept {
    form = NameForm::file_path;
    if (attributes.ObjectName == nullptr) {
        reason = "object-name-missing";
        return false;
    }
    if (attributes.ObjectName->Buffer == nullptr) {
        reason = "object-name-buffer-null";
        return false;
    }
    if (attributes.ObjectName->Length % sizeof(wchar_t) != 0) {
        reason = "object-name-odd-length";
        return false;
    }
    const size_t count = attributes.ObjectName->Length / sizeof(wchar_t);
    if (count == 0) {
        reason = "object-name-empty";
        return false;
    }
    if (count >= result.size()) {
        reason = "object-name-too-long";
        return false;
    }
    std::array<wchar_t, Capacity> name{};
    std::memcpy(name.data(), attributes.ObjectName->Buffer,
                count * sizeof(wchar_t));
    if (attributes.RootDirectory != nullptr && name[0] == L'\\') {
        // The system rejects an absolute name relative to a root handle with
        // STATUS_INVALID_PARAMETER; never ignore the root to open the name.
        form = NameForm::absolute_with_root;
        reason = "absolute-name-with-root";
        return false;
    }
    if (attributes.RootDirectory != nullptr) {
        const DWORD size = GetFinalPathNameByHandleW(
            attributes.RootDirectory, result.data(), DWORD(result.size()),
            FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (size == 0) {
            reason = "relative-root-not-file-handle";
            return false;
        }
        if (size + count + 1 >= result.size()) {
            reason = "relative-path-too-long";
            return false;
        }
        StripWin32Prefix(result.data());
        std::wcscat(result.data(), L"\\");
        std::wcscat(result.data(), name.data());
        return true;
    }
    if (name[0] != L'\\') {
        // Without a root handle the system requires a fully qualified name
        // (STATUS_OBJECT_PATH_SYNTAX_BAD). Never resolve a bare name against
        // the current directory.
        form = NameForm::relative_without_root;
        reason = "relative-name-without-root";
        return false;
    }
    if (std::wcsncmp(name.data(), L"\\??\\", 4) == 0) {
        // Only \??\X:\ designates a file path. Any other DOS device form
        // (device, Global\, pipe\, UNC\, Volume{...}, GLOBALROOT, bare X: or
        // X:relative) is refused for confinement, never turned into a file.
        const wchar_t* dos = name.data() + 4;
        if (DriveLetter(dos[0]) && dos[1] == L':') {
            if (dos[2] == L'\\') {
                std::wcscpy(result.data(), dos);
                return true;
            }
            form = dos[2] == L'\0' ? NameForm::dos_volume
                                   : NameForm::dos_drive_relative;
            reason =
                dos[2] == L'\0' ? "dos-volume-device" : "dos-drive-relative";
            return false;
        }
        const bool unc =
            CompareStringOrdinal(dos, 4, L"UNC\\", 4, TRUE) == CSTR_EQUAL;
        form = unc ? NameForm::dos_unc : NameForm::dos_device;
        reason = unc ? "dos-unc-path" : "dos-device-namespace";
        return false;
    }
    for (size_t index = 0; index < nativeVolumes.size(); ++index) {
        const auto& volume = nativeVolumes[index];
        const auto length = std::wcslen(volume.data());
        if (length > 0 && Prefix(name.data(), volume.data())) {
            result[0] = wchar_t(L'A' + index);
            result[1] = L':';
            std::wcscpy(result.data() + 2, name.data() + length);
            return true;
        }
    }
    reason = "native-object-not-file-volume";
    return false;
}

bool IsNativeInputOpen(const OBJECT_ATTRIBUTES* attributes,
                       std::array<wchar_t, Capacity>& path) noexcept {
    if (!attributes || !attributes->ObjectName ||
        !attributes->ObjectName->Buffer ||
        attributes->ObjectName->Length % sizeof(wchar_t) != 0) {
        return false;
    }
    const size_t length = attributes->ObjectName->Length / sizeof(wchar_t);
    if (length == 0 || length >= path.size()) {
        return false;
    }
    std::memcpy(path.data(), attributes->ObjectName->Buffer,
                length * sizeof(wchar_t));
    if (attributes->RootDirectory) {
        return false;
    }
    const auto input = tdur::mod::ClassifyNativeInputPath(
        std::wstring_view(path.data(), length));
    if (!input) {
        return false;
    }
    if (input->kind != tdur::mod::NativeInputKind::hid_interface) {
        return true;
    }
    std::array<wchar_t, Capacity> alias{};
    std::array<wchar_t, Capacity> target{};
    std::memcpy(alias.data(), input->alias.data(),
                input->alias.size() * sizeof(wchar_t));
    return QueryDosDeviceW(alias.data(), target.data(), DWORD(target.size())) !=
               0 &&
           tdur::mod::IsSimpleInputDeviceTarget(target.data());
}

NTSTATUS NTAPI ObserveNativeCreateFile(
    PHANDLE handle, ACCESS_MASK access, POBJECT_ATTRIBUTES attributes,
    PIO_STATUS_BLOCK status, PLARGE_INTEGER allocation, ULONG fileAttributes,
    ULONG sharing, ULONG disposition, ULONG options, PVOID ea, ULONG eaBytes) {
    constexpr ACCESS_MASK Writes = GENERIC_WRITE | GENERIC_ALL |
                                   FILE_WRITE_DATA | FILE_APPEND_DATA |
                                   FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES |
                                   DELETE | WRITE_DAC | WRITE_OWNER;
    const bool writing =
        (access & Writes) != 0 || disposition != 1 || (options & 0x1000u) != 0;
    if (internalOperation || !writing) {
        return realNativeCreateFile(handle, access, attributes, status,
                                    allocation, fileAttributes, sharing,
                                    disposition, options, ea, eaBytes);
    }
    InternalCall internal;
    std::array<wchar_t, Capacity> inputPath{};
    if (IsNativeInputOpen(attributes, inputPath)) {
        // Preserve the native device name and every original argument. Never
        // route an input-device symbolic link through DOS/CWD normalization.
        const auto result = realNativeCreateFile(
            handle, access, attributes, status, allocation, fileAttributes,
            sharing, disposition, options, ea, eaBytes);
        TdurObservationRecord("native-input-open-forwarded", DWORD(result),
                              inputPath.data());
        return result;
    }
    if (attributes && attributes->ObjectName &&
        attributes->ObjectName->Buffer &&
        attributes->ObjectName->Length % sizeof(wchar_t) == 0) {
        const auto length = attributes->ObjectName->Length / sizeof(wchar_t);
        if (length > 0 && length < inputPath.size()) {
            const auto service = tdur::mod::ClassifyNativeServicePath(
                std::wstring_view(inputPath.data(), length),
                attributes->RootDirectory != nullptr);
            bool forward = service.has_value();
#ifdef TDUR_MUTANT_BLOCK_AFD
            if (service == tdur::mod::NativeServiceKind::afd_endpoint) {
                forward = false;
            }
#endif
            if (forward) {
                const auto result = realNativeCreateFile(
                    handle, access, attributes, status, allocation,
                    fileAttributes, sharing, disposition, options, ea, eaBytes);
                TdurObservationRecord("native-service-open-forwarded",
                                      DWORD(result), inputPath.data());
                return result;
            }
        }
    }
    std::array<wchar_t, Capacity> windowsPath{};
    std::array<wchar_t, Capacity> canonical{};
    const char* reason = "attributes-missing";
    NameForm form = NameForm::file_path;
    bool missing_parent = false;
    bool trailing_separator = false;
    bool allowed = attributes != nullptr &&
                   NativePath(*attributes, windowsPath, reason, form);
    if (allowed) {
        reason = "noncanonical-or-reparse-file-path";
        allowed = CanonicalPath(windowsPath.data(), canonical, &missing_parent,
                                &trailing_separator);
        if (missing_parent)
            reason = "parent-directory-missing";
    }
    const bool protectedSource = allowed &&
                                 Prefix(canonical.data(), sourceRoot.data()) &&
                                 !Prefix(canonical.data(), privateRoot.data());
    const bool protectedInstallation =
        allowed && sourceInstallation[0] != L'\0' &&
        Prefix(canonical.data(), sourceInstallation.data());
    const bool destructiveAncestor =
        allowed && (access & (DELETE | WRITE_DAC | WRITE_OWNER)) != 0 &&
        (Prefix(sourceRoot.data(), canonical.data()) ||
         (sourceInstallation[0] != L'\0' &&
          Prefix(sourceInstallation.data(), canonical.data())));
#ifdef TDUR_MUTANT_ALLOW_SOURCE_WRITE
    const bool denySource = false;
#else
    const bool denySource = protectedSource;
#endif
    if (!allowed || denySource || protectedInstallation ||
        destructiveAncestor) {
        const bool nameForm = !allowed && form != NameForm::file_path;
        const auto* event =
            nameForm                ? "nt-name-form-refused"
            : !allowed              ? "unconfined-write-refused"
            : protectedInstallation ? "source-installation-write-refused"
            : destructiveAncestor   ? "source-ancestor-change-refused"
                                    : "source-write-refused";
        const auto [error, denied] = RefusalStatus(form, missing_parent);
        TdurObservationRecord(event, error,
                              allowed ? canonical.data()
                                      : (inputPath[0] != L'\0'
                                             ? inputPath.data()
                                             : windowsPath.data()),
                              allowed ? nullptr : reason);
        // Do not issue an open/create through an unresolved path. Preserve
        // missing-parent semantics so the caller can create its directories
        // and retry. Reparse/unknown paths and DOS device forms still return
        // access denied; the two NT syntax forms return the system status.
        if (handle) {
            *handle = nullptr;
        }
        if (status) {
            status->Status = denied;
            status->Information = 0;
        }
        return denied;
    }
    // Absolute DOS path plus OBJ_DONT_REPARSE prevents CWD and reparse
    // traversal changes between the guard and the actual kernel open. A
    // trailing separator of the request is restored after the checks, so the
    // system itself decides file versus directory (D2).
    std::array<wchar_t, Capacity + 8> absolute{};
    std::wcscpy(absolute.data(), L"\\??\\");
    std::wcscat(absolute.data(), canonical.data());
    if (trailing_separator) {
        std::wcscat(absolute.data(), L"\\");
    }
    UNICODE_STRING name{};
    name.Buffer = absolute.data();
    name.Length = USHORT(std::wcslen(name.Buffer) * sizeof(wchar_t));
    name.MaximumLength = USHORT(name.Length + sizeof(wchar_t));
    OBJECT_ATTRIBUTES confined = *attributes;
    confined.RootDirectory = nullptr;
    confined.ObjectName = &name;
    confined.Attributes |= 0x1000u; // OBJ_DONT_REPARSE, Windows 8+.
    const auto result = realNativeCreateFile(
        handle, access, &confined, status, allocation, fileAttributes, sharing,
        disposition, options, ea, eaBytes);
    TdurObservationRecord(
        protectedSource ? "mutant-source-write" : "file-write-observed",
        result >= 0 ? ERROR_SUCCESS : DWORD(result), canonical.data());
    return result;
}

// Returns nullptr when the token designates the current user (or is null),
// otherwise one bounded refusal reason. Never logs or returns a SID, a handle
// value or a path; preserves the caller's last error (D3).
const char* FolderTokenRefusal(HANDLE token) noexcept {
    if (token == nullptr)
        return nullptr;
    if (token == INVALID_HANDLE_VALUE)
        return "default-user-token"; // Default User is a different scope.
    const DWORD saved = GetLastError();
    constexpr DWORD bytes = sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE;
    alignas(TOKEN_USER) std::array<unsigned char, bytes> supplied{};
    alignas(TOKEN_USER) std::array<unsigned char, bytes> current{};
    const char* refusal = "token-not-queryable";
    DWORD supplied_size{};
    if (GetTokenInformation(token, TokenUser, supplied.data(), bytes,
                            &supplied_size)) {
        refusal = "process-token-unavailable";
        HANDLE process_token = nullptr;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY,
                             &process_token)) {
            DWORD current_size{};
            if (GetTokenInformation(process_token, TokenUser, current.data(),
                                    bytes, &current_size)) {
                const auto a =
                    reinterpret_cast<const TOKEN_USER*>(supplied.data());
                const auto b =
                    reinterpret_cast<const TOKEN_USER*>(current.data());
                if (!IsValidSid(a->User.Sid) || !IsValidSid(b->User.Sid))
                    refusal = "token-sid-invalid";
                else if (EqualSid(a->User.Sid, b->User.Sid))
                    refusal = nullptr;
                else
                    refusal = "token-other-user";
            }
            CloseHandle(process_token);
        }
    }
    SetLastError(saved);
    return refusal;
}

void RecordFolderRefusal(const char* event, HRESULT result, const char* reason,
                         const void* caller) noexcept {
    TdurObservationRecord(event, static_cast<DWORD>(result), nullptr, reason,
                          caller);
}

HRESULT WINAPI ObserveFolderA(HWND window, int folder, HANDLE token,
                              DWORD flags, LPSTR path) {
    const void* caller = _ReturnAddress();
    if ((folder & 0xff) != CSIDL_PERSONAL) {
        return realFolderA(window, folder, token, flags, path);
    }
    if (path == nullptr) {
        RecordFolderRefusal("folder-a-refused", E_POINTER, "null-output",
                            caller);
        return E_POINTER;
    }
    path[0] = '\0';
    if (const char* refusal = FolderTokenRefusal(token)) {
        RecordFolderRefusal("folder-a-refused", E_ACCESSDENIED, refusal,
                            caller);
        return E_ACCESSDENIED;
    }
    std::strcpy(path, privateAnsi.data());
    TdurObservationRecord("folder-a-redirected", ERROR_SUCCESS, nullptr,
                          token ? "current-user-token" : "null-token", caller);
    return S_OK;
}
HRESULT WINAPI ObserveFolderW(HWND window, int folder, HANDLE token,
                              DWORD flags, LPWSTR path) {
    const void* caller = _ReturnAddress();
    if ((folder & 0xff) != CSIDL_PERSONAL) {
        return realFolderW(window, folder, token, flags, path);
    }
    if (path == nullptr) {
        RecordFolderRefusal("folder-w-refused", E_POINTER, "null-output",
                            caller);
        return E_POINTER;
    }
    path[0] = L'\0';
    if (const char* refusal = FolderTokenRefusal(token)) {
        RecordFolderRefusal("folder-w-refused", E_ACCESSDENIED, refusal,
                            caller);
        return E_ACCESSDENIED;
    }
    std::wcscpy(path, privateRoot.data());
    TdurObservationRecord("folder-w-redirected", ERROR_SUCCESS, nullptr,
                          token ? "current-user-token" : "null-token", caller);
    return S_OK;
}
HRESULT WINAPI ObserveKnownFolder(REFKNOWNFOLDERID folder, DWORD flags,
                                  HANDLE token, PWSTR* path) {
    const void* caller = _ReturnAddress();
    if (!IsEqualGUID(folder, FOLDERID_Documents)) {
        return realKnownFolder(folder, flags, token, path);
    }
    if (path == nullptr) {
        RecordFolderRefusal("known-folder-refused", E_POINTER, "null-output",
                            caller);
        return E_POINTER;
    }
    *path = nullptr;
    if (const char* refusal = FolderTokenRefusal(token)) {
        RecordFolderRefusal("known-folder-refused", E_ACCESSDENIED, refusal,
                            caller);
        return E_ACCESSDENIED;
    }
    const size_t bytes =
        (std::wcslen(privateRoot.data()) + 1) * sizeof(wchar_t);
    auto* copy = static_cast<wchar_t*>(CoTaskMemAlloc(bytes));
    if (copy == nullptr) {
        RecordFolderRefusal("known-folder-refused", E_OUTOFMEMORY,
                            "allocation-failed", caller);
        return E_OUTOFMEMORY;
    }
    std::memcpy(copy, privateRoot.data(), bytes);
    *path = copy;
    TdurObservationRecord("known-folder-redirected", ERROR_SUCCESS, nullptr,
                          token ? "current-user-token" : "null-token", caller);
    return S_OK;
}

HINSTANCE WINAPI RefuseShellA(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, INT) {
    TdurObservationRecord("shell-execute-refused", ERROR_ACCESS_DENIED);
    SetLastError(ERROR_ACCESS_DENIED);
    return reinterpret_cast<HINSTANCE>(INT_PTR(SE_ERR_ACCESSDENIED));
}
HINSTANCE WINAPI RefuseShellW(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT) {
    TdurObservationRecord("shell-execute-refused", ERROR_ACCESS_DENIED);
    SetLastError(ERROR_ACCESS_DENIED);
    return reinterpret_cast<HINSTANCE>(INT_PTR(SE_ERR_ACCESSDENIED));
}
BOOL WINAPI RefuseShellExA(SHELLEXECUTEINFOA* info) {
    if (info) {
        info->hProcess = nullptr;
        info->hInstApp =
            reinterpret_cast<HINSTANCE>(INT_PTR(SE_ERR_ACCESSDENIED));
    }
    TdurObservationRecord("shell-execute-ex-refused", ERROR_ACCESS_DENIED);
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}
BOOL WINAPI RefuseShellExW(SHELLEXECUTEINFOW* info) {
    if (info) {
        info->hProcess = nullptr;
        info->hInstApp =
            reinterpret_cast<HINSTANCE>(INT_PTR(SE_ERR_ACCESSDENIED));
    }
    TdurObservationRecord("shell-execute-ex-refused", ERROR_ACCESS_DENIED);
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

BOOL RefuseChild(PROCESS_INFORMATION* information) noexcept {
    if (information != nullptr) {
        *information = {};
    }
    TdurObservationRecord("child-process-refused", ERROR_ACCESS_DENIED);
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}
BOOL WINAPI RefuseProcessA(LPCSTR, LPSTR, LPSECURITY_ATTRIBUTES,
                           LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCSTR,
                           LPSTARTUPINFOA, LPPROCESS_INFORMATION information) {
    return RefuseChild(information);
}
BOOL WINAPI RefuseProcessW(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES,
                           LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR,
                           LPSTARTUPINFOW, LPPROCESS_INFORMATION information) {
    return RefuseChild(information);
}
BOOL WINAPI RefuseAsUserA(HANDLE, LPCSTR, LPSTR, LPSECURITY_ATTRIBUTES,
                          LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCSTR,
                          LPSTARTUPINFOA, LPPROCESS_INFORMATION information) {
    return RefuseChild(information);
}
BOOL WINAPI RefuseAsUserW(HANDLE, LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES,
                          LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCWSTR,
                          LPSTARTUPINFOW, LPPROCESS_INFORMATION information) {
    return RefuseChild(information);
}
BOOL WINAPI RefuseWithLogon(LPCWSTR, LPCWSTR, LPCWSTR, DWORD, LPCWSTR, LPWSTR,
                            DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW,
                            LPPROCESS_INFORMATION information) {
    return RefuseChild(information);
}
BOOL WINAPI RefuseWithToken(HANDLE, DWORD, LPCWSTR, LPWSTR, DWORD, LPVOID,
                            LPCWSTR, LPSTARTUPINFOW,
                            LPPROCESS_INFORMATION information) {
    return RefuseChild(information);
}
UINT WINAPI RefuseWinExec(LPCSTR, UINT) {
    RefuseChild(nullptr);
    return ERROR_ACCESS_DENIED;
}

bool ReadRoot(const wchar_t* variable,
              std::array<wchar_t, Capacity>& output) noexcept {
    std::array<wchar_t, Capacity> value{};
    const DWORD size =
        GetEnvironmentVariableW(variable, value.data(), DWORD(value.size()));
    if (size == 0 || size >= value.size() ||
        !CanonicalPath(value.data(), output)) {
        return false;
    }
    const DWORD attributes = GetFileAttributesW(output.data());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}
} // namespace

void TdurObservationRecord(const char* event, DWORD error, const wchar_t* path,
                           const char* reason, const void* caller) noexcept {
    const DWORD saved = GetLastError();
    const char* recordedEvent = event;
    char statistics[256]{};
    for (auto& counter : frequentEvents) {
        if (std::strcmp(event, counter.name) != 0) {
            continue;
        }
        const auto count =
            counter.total.fetch_add(1, std::memory_order_relaxed) + 1;
        if (error != ERROR_SUCCESS) {
            counter.nonzero.fetch_add(1, std::memory_order_relaxed);
        }
        if (path == nullptr || path[0] == L'\0') {
            counter.emptyPaths.fetch_add(1, std::memory_order_relaxed);
        }
        const auto tick = GetTickCount64();
        uint64_t initialTick = 0;
        counter.lastTick.compare_exchange_strong(initialTick, tick,
                                                 std::memory_order_relaxed);
        if (count <= counter.individualLimit) {
            std::snprintf(statistics, sizeof(statistics),
                          " sample=%llu count_total=%llu",
                          static_cast<unsigned long long>(count),
                          static_cast<unsigned long long>(count));
        } else {
            if (tick - counter.lastTick.load(std::memory_order_relaxed) <
                    5000 ||
                counter.summaryLock.test_and_set(std::memory_order_acquire)) {
                SetLastError(saved);
                return;
            }
            if (tick - counter.lastTick.load(std::memory_order_relaxed) <
                5000) {
                counter.summaryLock.clear(std::memory_order_release);
                SetLastError(saved);
                return;
            }
            counter.lastTick.store(tick, std::memory_order_relaxed);
            const auto total = counter.total.load(std::memory_order_relaxed);
            const auto previousCount =
                counter.reported.exchange(total, std::memory_order_relaxed);
            recordedEvent = "observation-summary";
            std::snprintf(
                statistics, sizeof(statistics),
                " source_event=%s count_total=%llu count_since_last=%llu "
                "nonzero_results=%llu empty_paths_total=%llu",
                event, static_cast<unsigned long long>(total),
                static_cast<unsigned long long>(total - previousCount),
                static_cast<unsigned long long>(
                    counter.nonzero.load(std::memory_order_relaxed)),
                static_cast<unsigned long long>(
                    counter.emptyPaths.load(std::memory_order_relaxed)));
            counter.summaryLock.clear(std::memory_order_release);
        }
        break;
    }
    char utf8[2048]{};
    if (path != nullptr) {
        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, int(sizeof(utf8)),
                            nullptr, nullptr);
    }
    char callsite[80]{};
    if (caller && std::strcmp(recordedEvent, "observation-summary") != 0) {
        // Record only a main-image RVA, never an absolute address, external
        // module name, stack, or caller memory. The borrowed module handle is
        // compared only; it is neither dereferenced nor passed to FreeLibrary.
        HMODULE owner = nullptr;
        const auto main = GetModuleHandleW(nullptr);
        const char* kind = "unresolved";
        char relative[11] = "none";
        if (main &&
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(caller), &owner)) {
            if (owner != main) {
                kind = "other";
            } else {
                const auto address = reinterpret_cast<std::uintptr_t>(caller);
                const auto base = reinterpret_cast<std::uintptr_t>(main);
                if (address >= base && address - base <= UINT32_MAX) {
                    kind = "main";
                    std::snprintf(relative, sizeof(relative), "0x%08lx",
                                  static_cast<unsigned long>(address - base));
                }
            }
        }
        std::snprintf(callsite, sizeof(callsite),
                      " caller_image=%s caller_rva=%s", kind, relative);
    }
    char line[2784]{};
    const int size =
        std::snprintf(line, sizeof(line),
                      "project=TDU-Recompilation pid=%lu tid=%lu tick=%llu "
                      "event=%s error=%lu%s reason=%s%s path=%s\r\n",
                      static_cast<unsigned long>(GetCurrentProcessId()),
                      static_cast<unsigned long>(GetCurrentThreadId()),
                      static_cast<unsigned long long>(GetTickCount64()),
                      recordedEvent, static_cast<unsigned long>(error),
                      statistics, reason ? reason : "none", callsite, utf8);
    if (size > 0 && size_t(size) < sizeof(line) &&
        logFile != INVALID_HANDLE_VALUE) {
        AcquireSRWLockExclusive(&logLock);
        DWORD written = 0;
        WriteFile(logFile, line, DWORD(size), &written, nullptr);
        ReleaseSRWLockExclusive(&logLock);
    }
    SetLastError(saved);
}

bool TdurInstallObservationHooks() noexcept {
    InternalCall internal;
    if (!ReadRoot(L"TDUR_PRIVATE_DOCUMENTS", privateRoot) ||
        !ReadRoot(L"TDUR_SOURCE_DOCUMENTS", sourceRoot) ||
        _wcsicmp(privateRoot.data(), sourceRoot.data()) == 0 ||
        std::wcslen(privateRoot.data()) >= MAX_PATH) {
        return false;
    }
    SetLastError(ERROR_SUCCESS);
    const auto installationLength =
        GetEnvironmentVariableW(L"TDUR_SOURCE_INSTALLATION", nullptr, 0);
    if (installationLength > 0) {
        if (!ReadRoot(L"TDUR_SOURCE_INSTALLATION", sourceInstallation) ||
            Prefix(privateRoot.data(), sourceInstallation.data())) {
            return false;
        }
    } else if (GetLastError() != ERROR_ENVVAR_NOT_FOUND) {
        return false;
    }
    BOOL substituted = FALSE;
    const bool utf8ACP = GetACP() == CP_UTF8;
    if (WideCharToMultiByte(CP_ACP, utf8ACP ? 0 : WC_NO_BEST_FIT_CHARS,
                            privateRoot.data(), -1, privateAnsi.data(),
                            MAX_PATH, nullptr,
                            utf8ACP ? nullptr : &substituted) == 0 ||
        substituted) {
        return false;
    }
    std::array<wchar_t, Capacity> logPath{};
    std::array<wchar_t, Capacity> logInput{};
    const DWORD size = GetEnvironmentVariableW(
        L"TDUR_OBSERVATION_LOG", logInput.data(), DWORD(logInput.size()));
    if (size == 0 || size >= logInput.size() ||
        !CanonicalPath(logInput.data(), logPath) ||
        !Prefix(logPath.data(), privateRoot.data())) {
        return false;
    }
    logFile = CreateFileW(logPath.data(), FILE_APPEND_DATA,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (logFile == INVALID_HANDLE_VALUE) {
        return false;
    }
    for (size_t index = 0; index < nativeVolumes.size(); ++index) {
        const wchar_t drive[] = {wchar_t(L'A' + index), L':', L'\0'};
        QueryDosDeviceW(drive, nativeVolumes[index].data(), DWORD(Capacity));
    }
    const auto symbol =
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateFile");
    if (symbol == nullptr) {
        return false;
    }
    realNativeCreateFile = std::bit_cast<NativeCreateFile>(symbol);
    LONG result = DetourTransactionBegin();
    if (result == NO_ERROR) {
        result = DetourUpdateThread(GetCurrentThread());
    }
#ifndef TDUR_MUTANT_SKIP_FOLDER_HOOKS
    if (result == NO_ERROR) {
        result = DetourAttach(
            &realFolderA, static_cast<decltype(realFolderA)>(ObserveFolderA));
    }
    if (result == NO_ERROR) {
        result = DetourAttach(
            &realFolderW, static_cast<decltype(realFolderW)>(ObserveFolderW));
    }
    if (result == NO_ERROR) {
        result = DetourAttach(
            &realKnownFolder,
            static_cast<decltype(realKnownFolder)>(ObserveKnownFolder));
    }
#endif
    if (result == NO_ERROR) {
        result = DetourAttach(
            &realNativeCreateFile,
            static_cast<NativeCreateFile>(ObserveNativeCreateFile));
    }
    if (result == NO_ERROR) {
        result = DetourAttach(&realShellA,
                              static_cast<decltype(realShellA)>(RefuseShellA));
    }
    if (result == NO_ERROR) {
        result = DetourAttach(&realShellW,
                              static_cast<decltype(realShellW)>(RefuseShellW));
    }
    if (result == NO_ERROR) {
        result = DetourAttach(
            &realShellExA, static_cast<decltype(realShellExA)>(RefuseShellExA));
    }
    if (result == NO_ERROR) {
        result = DetourAttach(
            &realShellExW, static_cast<decltype(realShellExW)>(RefuseShellExW));
    }
    if (result == NO_ERROR) {
        result = DetourAttach(
            &realProcessA, static_cast<decltype(realProcessA)>(RefuseProcessA));
    }
    if (result == NO_ERROR) {
        result = DetourAttach(
            &realProcessW, static_cast<decltype(realProcessW)>(RefuseProcessW));
    }
    if (result == NO_ERROR) {
        result = DetourAttach(
            &realAsUserA, static_cast<decltype(realAsUserA)>(RefuseAsUserA));
    }
    if (result == NO_ERROR) {
        result = DetourAttach(
            &realAsUserW, static_cast<decltype(realAsUserW)>(RefuseAsUserW));
    }
    if (result == NO_ERROR) {
        result =
            DetourAttach(&realWithLogon,
                         static_cast<decltype(realWithLogon)>(RefuseWithLogon));
    }
    if (result == NO_ERROR) {
        result =
            DetourAttach(&realWithToken,
                         static_cast<decltype(realWithToken)>(RefuseWithToken));
    }
    if (result == NO_ERROR) {
        result = DetourAttach(
            &realWinExec, static_cast<decltype(realWinExec)>(RefuseWinExec));
    }
    if (result != NO_ERROR) {
        DetourTransactionAbort();
    } else {
        result = DetourTransactionCommit();
    }
    TdurObservationRecord(result == NO_ERROR ? "hooks-installed-pre-entry"
                                             : "hooks-install-failed",
                          DWORD(result));
    return result == NO_ERROR;
}
