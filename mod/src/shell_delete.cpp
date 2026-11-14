// SHFileOperationA and SHFileOperationW hooks. NEW POLICY, never a
// recovered routine of the game. A delete request that the DLL can perform
// exactly as the SHFileOperation documentation describes it (FO_DELETE,
// FOF_NOCONFIRMATION, no user interface, no recycle bin, no wildcard, no
// named stream), whose paths all lie strictly under the session's private
// save root, is performed here on the calling thread with the engine of
// shell_delete_core.cpp: no Shell, no COM, no copy engine handler. Any other
// request is forwarded unchanged, after a journal line written before the
// call.
#include "tdur/mod/shell_delete.hpp"
#include "tdur/mod/observation_hooks.hpp"

#include <detours.h>
#include <intrin.h>
#include <objbase.h>
#include <shellapi.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

namespace {
namespace sd = tdur::mod::shell_delete;

auto realOperationA = &SHFileOperationA;
auto realOperationW = &SHFileOperationW;

// Bound of the path field of one journal line: 600 UTF-16 units stay below
// the 2048 UTF-8 bytes that TdurObservationRecord converts.
constexpr std::size_t LoggedChars = 600;
constexpr std::size_t CanonicalChars = 1024;

const char* ApartmentName() noexcept {
    APTTYPE type{};
    APTTYPEQUALIFIER qualifier{};
    const HRESULT result = CoGetApartmentType(&type, &qualifier);
    if (result == CO_E_NOTINITIALIZED) {
        return "none";
    }
    if (FAILED(result)) {
        return "unknown";
    }
    switch (type) {
    case APTTYPE_STA:
        return "sta";
    case APTTYPE_MTA:
        return "mta";
    case APTTYPE_NA:
        return "na";
    case APTTYPE_MAINSTA:
        return "mainsta";
    default:
        return "unknown";
    }
}

void AppendLogged(std::wstring& logged, std::wstring_view path) {
    if (logged.size() >= LoggedChars) {
        return;
    }
    if (!logged.empty()) {
        logged.push_back(L'|');
    }
    const std::size_t room = LoggedChars - logged.size();
    if (path.size() > room) {
        logged.append(path.substr(0, room));
        logged.append(L"...");
    } else {
        logged.append(path);
    }
}

std::size_t Length(const char* text) noexcept { return std::strlen(text); }
std::size_t Length(const wchar_t* text) noexcept { return std::wcslen(text); }

// Wide form of one list entry. A: CP_ACP with MB_ERR_INVALID_CHARS; on
// failure `strict` is false and `wide` holds a lossy form for the journal.
void Widen(const char* text, std::size_t length, std::wstring& wide,
           bool& strict) {
    wchar_t buffer[MAX_PATH]{};
    int size = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, text,
                                   int(length), buffer, MAX_PATH - 1);
    if (size <= 0) {
        strict = false;
        size = MultiByteToWideChar(CP_ACP, 0, text, int(length), buffer,
                                   MAX_PATH - 1);
    }
    wide.assign(buffer, size > 0 ? std::size_t(size) : 0);
}
void Widen(const wchar_t* text, std::size_t length, std::wstring& wide, bool&) {
    wide.assign(text, length);
}

template <class Operation, class Real>
int Forward(Operation* operation, Real real, sd::Cause cause, char api,
            const std::wstring& logged, const char* apartment,
            const void* caller) {
    char reason[192]{};
    std::snprintf(
        reason, sizeof(reason), "%s api=%c func=%u flags=0x%08lx apartment=%s",
        sd::CauseName(cause), api,
        operation != nullptr ? static_cast<unsigned>(operation->wFunc) : 0u,
        static_cast<unsigned long>(operation != nullptr ? operation->fFlags
                                                        : 0),
        apartment);
    // Written before the call, so that a call that never returns leaves its
    // trace.
    TdurObservationRecord("shell-delete-forwarded", ERROR_SUCCESS,
                          logged.c_str(), reason, caller);
    const ULONGLONG start = GetTickCount64();
    const int result = real(operation);
    char returned[224]{};
    std::snprintf(returned, sizeof(returned), "%s ms=%llu", reason,
                  static_cast<unsigned long long>(GetTickCount64() - start));
    TdurObservationRecord("shell-delete-forwarded-returned", DWORD(result),
                          logged.c_str(), returned, caller);
    return result;
}

template <class Operation, class Real>
int Intercept(Operation* operation, Real real, char api,
              const void* caller) noexcept {
    const char* apartment = ApartmentName();
    std::wstring logged;
    try {
        if (operation == nullptr) {
            return Forward(operation, real, sd::Cause::null_request, api,
                           logged, apartment, caller);
        }
        const auto* list = operation->pFrom;
        const sd::ListScan scan = sd::ScanList(list);
        std::wstring paths; // Wide entries, NUL-separated.
        bool strict = true;
        if (scan.cause != sd::Cause::list_invalid) {
            std::wstring wide;
            for (const auto* entry = list; *entry != 0;) {
                const std::size_t length = Length(entry);
                Widen(entry, length, wide, strict);
                AppendLogged(logged, wide);
                paths.append(wide);
                paths.push_back(L'\0');
                entry += length + 1;
            }
        }
        sd::Cause cause =
            sd::DecideOperation(operation->wFunc, operation->fFlags);
        if (cause == sd::Cause::none) {
            cause = scan.cause;
        }
        if (cause == sd::Cause::none && !strict) {
            cause = sd::Cause::ansi_conversion;
        }
        std::wstring canonicals; // NUL-separated.
        std::wstring handled;    // Journal form.
        for (std::size_t offset = 0;
             cause == sd::Cause::none && offset < paths.size();) {
            const wchar_t* path = paths.c_str() + offset;
            offset += std::wcslen(path) + 1;
            wchar_t canonical[CanonicalChars]{};
            if (sd::HasWildcard(path)) {
                cause = sd::Cause::wildcard;
            } else if (!TdurPrivateSaveTarget(path, canonical,
                                              CanonicalChars)) {
                cause = sd::Cause::outside_root;
            } else if (sd::NamesStream(canonical)) {
                // A named stream may belong, through a hard link, to a
                // file outside the root.
                cause = sd::Cause::stream;
            } else {
                canonicals.append(canonical);
                canonicals.push_back(L'\0');
                AppendLogged(handled, canonical);
            }
        }
        wchar_t root[CanonicalChars]{};
        if (cause == sd::Cause::none &&
            !TdurPrivateSaveRoot(root, CanonicalChars)) {
            cause = sd::Cause::outside_root;
        }
        if (cause != sd::Cause::none) {
            return Forward(operation, real, cause, api, logged, apartment,
                           caller);
        }
        const ULONGLONG start = GetTickCount64();
        sd::DeleteTally tally;
        // Inside the internal operation scope, the NtCreateFile guard passes
        // the engine's opens unchanged: it would otherwise refuse a leaf
        // reparse point that the engine must delete as a link and rewrite
        // the opens relative to a held directory as absolute ones. The
        // engine verifies every object itself; no save-write-observed line
        // is written for its opens.
        const bool previous = TdurEnterInternalOperation();
        for (std::size_t offset = 0; offset < canonicals.size();) {
            const wchar_t* path = canonicals.c_str() + offset;
            offset += std::wcslen(path) + 1;
            sd::DeleteUnderRoot(path, root, tally);
        }
        TdurLeaveInternalOperation(previous);
        operation->fAnyOperationsAborted = FALSE;
        char reason[224]{};
        std::snprintf(reason, sizeof(reason),
                      "handled api=%c files=%u dirs=%u ms=%llu flags=0x%08lx "
                      "apartment=%s paths=%lu",
                      api, tally.files, tally.directories,
                      static_cast<unsigned long long>(GetTickCount64() - start),
                      static_cast<unsigned long>(operation->fFlags), apartment,
                      static_cast<unsigned long>(scan.count));
        TdurObservationRecord("shell-delete-handled", tally.error,
                              handled.c_str(), reason, caller);
        return static_cast<int>(tally.error);
    } catch (...) {
        // Memory exhausted while reading the request: forward it unchanged.
        return Forward(operation, real, sd::Cause::list_invalid, api,
                       std::wstring(), apartment, caller);
    }
}

int WINAPI ShellDeleteA(LPSHFILEOPSTRUCTA operation) {
    return Intercept(operation, realOperationA, 'A', _ReturnAddress());
}
int WINAPI ShellDeleteW(LPSHFILEOPSTRUCTW operation) {
    return Intercept(operation, realOperationW, 'W', _ReturnAddress());
}
} // namespace

bool TdurInstallShellDeleteHooks() noexcept {
    // The engine works inside the guard's internal operation scope, which
    // lifts the guard's refusals for the source Documents, the source
    // installation and their ancestors: a save root that holds either of
    // them is refused, and the DLL then refuses to load.
    wchar_t root[CanonicalChars]{};
    if (!TdurPrivateSaveRoot(root, CanonicalChars)) {
        TdurObservationRecord("shell-delete-hooks-install-failed",
                              ERROR_ACCESS_DENIED, nullptr,
                              "save-root-refused");
        return false;
    }
    LONG result = DetourTransactionBegin();
    if (result == NO_ERROR) {
        result = DetourUpdateThread(GetCurrentThread());
    }
    if (result == NO_ERROR) {
        result =
            DetourAttach(&realOperationA,
                         static_cast<decltype(realOperationA)>(ShellDeleteA));
    }
    if (result == NO_ERROR) {
        result =
            DetourAttach(&realOperationW,
                         static_cast<decltype(realOperationW)>(ShellDeleteW));
    }
    if (result != NO_ERROR) {
        DetourTransactionAbort();
    } else {
        result = DetourTransactionCommit();
    }
    TdurObservationRecord(result == NO_ERROR
                              ? "shell-delete-hooks-installed"
                              : "shell-delete-hooks-install-failed",
                          DWORD(result));
    return result == NO_ERROR;
}
