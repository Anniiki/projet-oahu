// Decisions and deletion engine of the Shell-free save folder
// deletion. NEW POLICY, never a recovered routine of the game; see
// docs/evidence/shell_delete_without_shell.md. Win32 plus two ntdll exports
// resolved at run time (NtCreateFile, RtlNtStatusToDosError): no Detours, no
// journal, so that tests/shell_delete_core_test.cpp runs it on its own.
#include "tdur/mod/shell_delete.hpp"

#include <winternl.h>

#include <bit>
#include <cstring>
#include <cwchar>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace tdur::mod::shell_delete {
namespace {

// FileDispositionInfoEx and its flags exist since Windows 10 1607 (the
// read-only flag since 1809); the SDK hides them behind version macros, so
// they are spelled out here with their documented values.
constexpr auto DispositionInfoEx =
    static_cast<FILE_INFO_BY_HANDLE_CLASS>(FileIdExtdDirectoryRestartInfo + 1);
constexpr DWORD DispositionDelete = 0x00000001;
constexpr DWORD DispositionPosix = 0x00000002;
constexpr DWORD DispositionIgnoreReadOnly = 0x00000010;
struct DispositionEx {
    DWORD flags;
};

// Attributes that FileBasicInfo may set; the others are ignored or refused.
constexpr DWORD SettableAttributes =
    FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM |
    FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_OFFLINE |
    FILE_ATTRIBUTE_NOT_CONTENT_INDEXED;

// The root and every directory between the root and the target are held
// with FILE_LIST_DIRECTORY: an open that asks for data access enters the
// sharing check, so that, opened without FILE_SHARE_DELETE, it keeps every
// other handle from obtaining the DELETE access that renaming, moving or
// deleting that directory requires. An open limited to attributes would not
// enter the check and would hold nothing.
constexpr DWORD HeldAccess =
    FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES | SYNCHRONIZE;
// An object to delete: DELETE, which enters the sharing check too, and
// FILE_READ_ATTRIBUTES. A plain directory adds FILE_LIST_DIRECTORY; only the
// read-only fallback asks for FILE_WRITE_ATTRIBUTES, on a second handle.
constexpr DWORD DeleteAccess = DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE;
// No FILE_SHARE_DELETE, see above.
constexpr DWORD ObjectSharing = FILE_SHARE_READ | FILE_SHARE_WRITE;
// Opens by path, the root and the probe of the target: the last component
// is never followed, and a named pipe reached through a reparse point would
// only see an anonymous client.
constexpr DWORD PathFlags = FILE_FLAG_BACKUP_SEMANTICS |
                            FILE_FLAG_OPEN_REPARSE_POINT |
                            SECURITY_SQOS_PRESENT | SECURITY_ANONYMOUS;
// Opens relative to a held directory: the entry itself is never followed,
// and OBJ_DONT_REPARSE refuses any reparse point met while parsing the name.
constexpr ULONG RelativeOptions =
    FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT;
constexpr ULONG RelativeAttributes = OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE;
constexpr std::size_t FinalPathLimit = 32768;
constexpr DWORD ListingBytes = 64 * 1024;

using CreateFileFunction = NTSTATUS(NTAPI*)(PHANDLE, ACCESS_MASK,
                                            POBJECT_ATTRIBUTES,
                                            PIO_STATUS_BLOCK, PLARGE_INTEGER,
                                            ULONG, ULONG, ULONG, ULONG, PVOID,
                                            ULONG);
using StatusFunction = ULONG(NTAPI*)(NTSTATUS);

// An export of ntdll, which every process maps; null when it is missing.
// In the DLL, NtCreateFile reaches the write guard's hook, which passes the
// call unchanged inside the internal operation scope.
template <class Function> Function NtdllExport(const char* name) noexcept {
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    return ntdll != nullptr
               ? std::bit_cast<Function>(GetProcAddress(ntdll, name))
               : nullptr;
}

template <class Char> bool IsWildcard(Char character) noexcept {
    return character == static_cast<Char>('*') ||
           character == static_cast<Char>('?');
}

template <class Char> ListScan Scan(const Char* list) noexcept {
    ListScan scan;
    if (list == nullptr) {
        return scan;
    }
    std::size_t used = 0;
    std::size_t length = 0;
    bool wildcard = false;
    bool tooLong = false;
    while (used < ListLimit) {
        const Char character = list[used++];
        if (character != 0) {
            ++length;
            wildcard = wildcard || IsWildcard(character);
            continue;
        }
        if (length == 0) {
            // An empty string ends the list; an empty first entry is an
            // empty list.
            if (scan.count == 0) {
                return scan;
            }
            scan.cause = tooLong    ? Cause::path_too_long
                         : wildcard ? Cause::wildcard
                                    : Cause::none;
            return scan;
        }
        tooLong = tooLong || length >= PathLimit;
        ++scan.count;
        length = 0;
    }
    scan.count = 0; // No terminator within the bound.
    return scan;
}

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE handle) noexcept : value(handle) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(other.value) {
        other.value = INVALID_HANDLE_VALUE;
    }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            Close();
            value = other.value;
            other.value = INVALID_HANDLE_VALUE;
        }
        return *this;
    }
    ~Handle() { Close(); }
    void Close() noexcept {
        if (value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
            value = INVALID_HANDLE_VALUE;
        }
    }
    bool Valid() const noexcept { return value != INVALID_HANDLE_VALUE; }
};

// \\?\ form of an absolute path: drive-letter paths gain the prefix, a path
// that already holds it is kept.
std::wstring Prefixed(std::wstring_view path) {
    if (path.size() >= 4 && path.substr(0, 4) == L"\\\\?\\") {
        return std::wstring(path);
    }
    return L"\\\\?\\" + std::wstring(path);
}

Handle OpenPath(const std::wstring& path, DWORD access,
                DWORD sharing) noexcept {
    return Handle(CreateFileW(path.c_str(), access, sharing, nullptr,
                              OPEN_EXISTING, PathFlags, nullptr));
}

// Final path of the object behind `handle`, as the system names it now
// (normalized, DOS volume name, \\?\ form). `buffer` is the engine's one
// shared buffer of FinalPathLimit characters.
DWORD FinalPath(HANDLE handle, std::wstring& buffer, std::wstring& path) {
    path.clear();
    buffer.resize(FinalPathLimit);
    const DWORD length =
        GetFinalPathNameByHandleW(handle, buffer.data(), DWORD(buffer.size()),
                                  FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (length == 0) {
        const DWORD error = GetLastError();
        return error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;
    }
    if (length >= buffer.size()) {
        return ERROR_FILENAME_EXCED_RANGE;
    }
    path.assign(buffer.data(), length);
    return ERROR_SUCCESS;
}

bool SamePath(std::wstring_view left, std::wstring_view right) noexcept {
    return left.size() == right.size() && !left.empty() &&
           CompareStringOrdinal(left.data(), int(left.size()), right.data(),
                                int(right.size()), TRUE) == CSTR_EQUAL;
}

// A colon after the drive letter names a stream: no path that the engine
// opens or reads back on a handle may hold one.
bool StreamFree(std::wstring_view path) noexcept { return !NamesStream(path); }

DWORD ReadAttributes(HANDLE handle, DWORD& attributes) noexcept {
    FILE_ATTRIBUTE_TAG_INFO information{};
    if (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo,
                                      &information, sizeof(information))) {
        return GetLastError();
    }
    attributes = information.FileAttributes;
    return ERROR_SUCCESS;
}

// An object with more than one name shares its data, attributes, times and
// streams with names that may lie outside the root: it is never deleted, and
// its attributes are never changed.
DWORD SingleName(HANDLE handle) noexcept {
    FILE_STANDARD_INFO standard{};
    if (!GetFileInformationByHandleEx(handle, FileStandardInfo, &standard,
                                      sizeof(standard))) {
        const DWORD error = GetLastError();
        return error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;
    }
    return standard.NumberOfLinks == 1 ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
}

// The same object behind both handles: volume serial number and file index.
bool SameObject(HANDLE left, HANDLE right) noexcept {
    BY_HANDLE_FILE_INFORMATION first{};
    BY_HANDLE_FILE_INFORMATION second{};
    return GetFileInformationByHandle(left, &first) &&
           GetFileInformationByHandle(right, &second) &&
           first.dwVolumeSerialNumber == second.dwVolumeSerialNumber &&
           first.nFileIndexHigh == second.nFileIndexHigh &&
           first.nFileIndexLow == second.nFileIndexLow;
}

DWORD Disposition(HANDLE handle) noexcept {
    FILE_DISPOSITION_INFO disposition{TRUE};
    if (SetFileInformationByHandle(handle, FileDispositionInfo, &disposition,
                                   sizeof(disposition))) {
        return ERROR_SUCCESS;
    }
    const DWORD error = GetLastError();
    return error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;
}

// An entry of a directory listing: its name, and the attributes the listing
// gave, which only choose how the entry is opened.
struct Listed {
    std::wstring name;
    DWORD attributes = 0;
};

class Engine {
  public:
    Engine(DeleteTally& tally, const DeleteOptions& options)
        : tally_(tally), options_(options),
          sharing_(options.share_delete ? ObjectSharing | FILE_SHARE_DELETE
                                        : ObjectSharing),
          create_(NtdllExport<CreateFileFunction>("NtCreateFile")),
          status_(NtdllExport<StatusFunction>("RtlNtStatusToDosError")) {}

    void Run(const wchar_t* target, const wchar_t* root) {
        if (target == nullptr || root == nullptr || *target == L'\0' ||
            *root == L'\0') {
            Fail(ERROR_INVALID_PARAMETER);
            return;
        }
        if (create_ == nullptr || status_ == nullptr) {
            Fail(ERROR_PROC_NOT_FOUND);
            return;
        }
        const std::wstring requested = Prefixed(target);
        if (!StreamFree(requested)) {
            Fail(ERROR_INVALID_NAME);
            return;
        }
        // 1. The root: a plain directory, never a reparse point, held
        // (FILE_LIST_DIRECTORY, no FILE_SHARE_DELETE) until the end.
        Handle rootHandle = OpenPath(Prefixed(root), HeldAccess, sharing_);
        if (!rootHandle.Valid()) {
            Fail(GetLastError());
            return;
        }
        DWORD attributes = 0;
        if (const DWORD error = ReadAttributes(rootHandle.value, attributes)) {
            Fail(error);
            return;
        }
        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            Fail(ERROR_ACCESS_DENIED);
            return;
        }
        if (const DWORD error = FinalPath(rootHandle.value, buffer_, root_)) {
            Fail(error);
            return;
        }
        // A folder above the root replaced by a junction would move the
        // root elsewhere: its final path must be the path it was named by.
        if (!SamePath(root_, Prefixed(root))) {
            Fail(ERROR_ACCESS_DENIED);
            return;
        }
        if (!StreamFree(root_)) {
            Fail(ERROR_INVALID_NAME);
            return;
        }
        // 2. Where the target is. This handle only reads attributes and is
        // closed at once: the target is opened again below, relative to the
        // last directory held. Its final path must be the very path
        // requested, so that a component turned into a junction since the
        // decision is refused, never resolved to another object.
        std::wstring located;
        DWORD found = 0;
        {
            Handle probe = OpenPath(requested, FILE_READ_ATTRIBUTES, sharing_);
            if (!probe.Valid()) {
                Fail(GetLastError());
                return;
            }
            if (const DWORD error = FinalPath(probe.value, buffer_, located)) {
                Fail(error);
                return;
            }
            if (const DWORD error = ReadAttributes(probe.value, found)) {
                Fail(error);
                return;
            }
        }
        if (!StreamFree(located)) {
            Fail(ERROR_INVALID_NAME);
            return;
        }
        if (!SamePath(located, requested)) {
            Fail(ERROR_ACCESS_DENIED);
            return;
        }
        if (!StrictlyUnder(located, root_)) {
            Fail(ERROR_ACCESS_DENIED);
            return;
        }
        // 3. Every directory between the root and the target is opened
        // relative to the handle of the previous one, checked on its own
        // handle, and held until the end.
        std::vector<Handle> chain;
        HANDLE parent = rootHandle.value;
        std::wstring parentPath = root_;
        std::size_t start = root_.size();
        while (start < located.size() && located[start] == L'\\') {
            ++start;
        }
        for (;;) {
            const std::size_t end = located.find(L'\\', start);
            if (end == std::wstring::npos) {
                break;
            }
            const std::wstring_view name(located.data() + start, end - start);
            Handle directory;
            std::wstring actual;
            if (const DWORD error = OpenRelative(
                    parent, parentPath, name, HeldAccess,
                    FILE_ATTRIBUTE_DIRECTORY, directory, actual, attributes)) {
                Fail(error);
                return;
            }
            if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
                (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                Fail(ERROR_ACCESS_DENIED);
                return;
            }
            parent = directory.value;
            chain.push_back(std::move(directory));
            parentPath = std::move(actual);
            start = end + 1;
        }
        if (options_.after_chain != nullptr) {
            options_.after_chain(parentPath.c_str(), root_.c_str(),
                                 options_.context);
        }
        Entry(parent, parentPath, std::wstring_view(located).substr(start),
              found, 0);
    }

  private:
    void Fail(DWORD error) noexcept {
        if (tally_.error == ERROR_SUCCESS) {
            tally_.error = error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;
        }
    }

    DWORD StatusError(NTSTATUS status) const noexcept {
        const ULONG error = status_(status);
        return error != ERROR_SUCCESS ? error : ERROR_GEN_FAILURE;
    }

    // Opens the entry `name` of the held directory `parent`, whose final
    // path is `parentPath`, relative to that handle: NtCreateFile with
    // RootDirectory and the entry name alone, so that no other component is
    // parsed by name and no reparse point is followed. `listed` (attributes
    // from the parent's listing, or from the probe) chooses
    // FILE_DIRECTORY_FILE or FILE_NON_DIRECTORY_FILE, so that an entry that
    // changed kind since fails to open. Then checks on the new handle that
    // its final path is `parentPath`\`name`, free of streams and strictly
    // under the root. On success `handle`, `actual` and `attributes` (read
    // on the handle) describe the object.
    DWORD OpenRelative(HANDLE parent, const std::wstring& parentPath,
                       std::wstring_view name, DWORD access, DWORD listed,
                       Handle& handle, std::wstring& actual,
                       DWORD& attributes) {
        if (parent == nullptr || parent == INVALID_HANDLE_VALUE) {
            return ERROR_INVALID_HANDLE;
        }
        if (name.empty() || name == L"." || name == L".." ||
            name.find(L'\\') != std::wstring_view::npos ||
            name.size() * sizeof(wchar_t) > 0xFFFE) {
            return ERROR_INVALID_NAME;
        }
        std::wstring expected = parentPath;
        if (expected.empty() || expected.back() != L'\\') {
            expected.push_back(L'\\');
        }
        expected.append(name);
        if (!StreamFree(expected)) {
            return ERROR_INVALID_NAME;
        }
        const std::wstring_view opened = name;
        UNICODE_STRING objectName{};
        objectName.Buffer = const_cast<wchar_t*>(opened.data());
        objectName.Length =
            static_cast<USHORT>(opened.size() * sizeof(wchar_t));
        objectName.MaximumLength = objectName.Length;
        OBJECT_ATTRIBUTES object{};
        object.Length = sizeof(object);
        object.RootDirectory = parent;
        object.ObjectName = &objectName;
        object.Attributes = RelativeAttributes;
        const ULONG options =
            RelativeOptions | ((listed & FILE_ATTRIBUTE_DIRECTORY) != 0
                                   ? FILE_DIRECTORY_FILE
                                   : FILE_NON_DIRECTORY_FILE);
        IO_STATUS_BLOCK io{};
        HANDLE value = nullptr;
        const NTSTATUS status =
            create_(&value, access, &object, &io, nullptr, 0, sharing_,
                    FILE_OPEN, options, nullptr, 0);
        if (status < 0) {
            return StatusError(status);
        }
        handle = Handle(value);
        if (const DWORD error = FinalPath(handle.value, buffer_, actual)) {
            return error;
        }
        if (!SamePath(actual, expected) || !StrictlyUnder(actual, root_)) {
            return ERROR_ACCESS_DENIED;
        }
        if (!StreamFree(actual)) {
            return ERROR_INVALID_NAME;
        }
        return ReadAttributes(handle.value, attributes);
    }

    // Entries of an open directory, read through its handle.
    DWORD List(HANDLE directory, std::vector<Listed>& entries) {
        auto buffer = std::make_unique<unsigned char[]>(ListingBytes);
        FILE_INFO_BY_HANDLE_CLASS kind = FileFullDirectoryRestartInfo;
        for (;;) {
            if (!GetFileInformationByHandleEx(directory, kind, buffer.get(),
                                              ListingBytes)) {
                const DWORD error = GetLastError();
                return EndOfListing(error, kind == FileFullDirectoryRestartInfo)
                           ? ERROR_SUCCESS
                           : error;
            }
            kind = FileFullDirectoryInfo;
            std::size_t offset = 0;
            for (;;) {
                const auto* entry = reinterpret_cast<const FILE_FULL_DIR_INFO*>(
                    buffer.get() + offset);
                const std::wstring_view name(
                    entry->FileName, entry->FileNameLength / sizeof(wchar_t));
                if (name != L"." && name != L"..") {
                    entries.push_back(
                        {std::wstring(name), entry->FileAttributes});
                }
                if (entry->NextEntryOffset == 0) {
                    break;
                }
                offset += entry->NextEntryOffset;
                if (offset >= ListingBytes) {
                    return ERROR_INVALID_DATA;
                }
            }
        }
    }

    // Deletes the entry `name` of the held directory `parent` (and, for a
    // plain directory, everything below it). Returns true when it is gone.
    bool Entry(HANDLE parent, const std::wstring& parentPath,
               std::wstring_view name, DWORD listed, unsigned depth) {
        const bool listable = (listed & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
                              (listed & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
        Handle handle;
        std::wstring actual;
        DWORD attributes = 0;
        if (const DWORD error = OpenRelative(
                parent, parentPath, name,
                DeleteAccess | (listable ? FILE_LIST_DIRECTORY : 0), listed,
                handle, actual, attributes)) {
            Fail(error);
            return false;
        }
        const DWORD names = SingleName(handle.value);
        if (names != ERROR_SUCCESS) {
            Fail(names);
            return false;
        }
        const bool directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        const bool reparse = (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        if (directory && !reparse) {
            if (depth >= options_.max_depth) {
                Fail(ERROR_FILENAME_EXCED_RANGE);
                return false;
            }
            std::vector<Listed> entries;
            if (const DWORD error = List(handle.value, entries)) {
                Fail(error);
                return false;
            }
            if (options_.after_listing != nullptr) {
                options_.after_listing(actual.c_str(), options_.context);
            }
            bool emptied = true;
            for (const auto& child : entries) {
                emptied = Entry(handle.value, actual, child.name,
                                child.attributes, depth + 1) &&
                          emptied;
            }
            if (!emptied) {
                return false;
            }
        }
        if (const DWORD error = Dispose(handle.value, parent, parentPath, name,
                                        attributes, listed)) {
            Fail(error);
            return false;
        }
        // POSIX semantics: the name leaves the directory when this handle
        // closes, so that the parent can be deleted next.
        handle.Close();
        if (directory) {
            ++tally_.directories;
        } else {
            ++tally_.files;
        }
        return true;
    }

    // Deletes the object behind `handle`, the entry `name` of the held
    // directory `parent`.
    DWORD Dispose(HANDLE handle, HANDLE parent, const std::wstring& parentPath,
                  std::wstring_view name, DWORD attributes, DWORD listed) {
        if (!options_.legacy_disposition) {
            DispositionEx extended{DispositionDelete | DispositionPosix |
                                   DispositionIgnoreReadOnly};
            if (SetFileInformationByHandle(handle, DispositionInfoEx, &extended,
                                           sizeof(extended))) {
                return ERROR_SUCCESS;
            }
            const DWORD error = GetLastError();
            if (error != ERROR_INVALID_PARAMETER &&
                error != ERROR_NOT_SUPPORTED) {
                return error;
            }
        }
        // Fallback for a volume that refuses the extended form.
        if ((attributes & FILE_ATTRIBUTE_READONLY) == 0) {
            return Disposition(handle);
        }
        // A read-only object: its attribute is cleared through a second
        // handle, the only one that asks for FILE_WRITE_ATTRIBUTES, opened
        // relative to the held parent like the first and checked again (same
        // final path, same object, still a single name).
        Handle writer;
        std::wstring again;
        DWORD current = 0;
        if (const DWORD error = OpenRelative(
                parent, parentPath, name,
                FILE_WRITE_ATTRIBUTES | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                listed, writer, again, current)) {
            return error;
        }
        if (!SameObject(handle, writer.value)) {
            return ERROR_ACCESS_DENIED;
        }
        if (const DWORD error = SingleName(writer.value)) {
            return error;
        }
        FILE_BASIC_INFO basic{};
        if (!GetFileInformationByHandleEx(writer.value, FileBasicInfo, &basic,
                                          sizeof(basic))) {
            return GetLastError();
        }
        FILE_BASIC_INFO change{}; // Zero times: left unchanged.
        change.FileAttributes = basic.FileAttributes & SettableAttributes &
                                ~DWORD(FILE_ATTRIBUTE_READONLY);
        if (change.FileAttributes == 0) {
            change.FileAttributes = FILE_ATTRIBUTE_NORMAL;
        }
        if (!SetFileInformationByHandle(writer.value, FileBasicInfo, &change,
                                        sizeof(change))) {
            return GetLastError();
        }
        const DWORD error = Disposition(handle);
        if (error != ERROR_SUCCESS) {
            FILE_BASIC_INFO restore{};
            restore.FileAttributes = basic.FileAttributes & SettableAttributes;
            if (restore.FileAttributes == 0) {
                restore.FileAttributes = FILE_ATTRIBUTE_NORMAL;
            }
            SetFileInformationByHandle(writer.value, FileBasicInfo, &restore,
                                       sizeof(restore));
        }
        return error;
    }

    DeleteTally& tally_;
    const DeleteOptions& options_;
    DWORD sharing_;
    CreateFileFunction create_;
    StatusFunction status_;
    std::wstring root_;   // Final path of the root, \\?\ form.
    std::wstring buffer_; // Shared final path buffer.
};

} // namespace

const char* CauseName(Cause cause) noexcept {
    switch (cause) {
    case Cause::none:
        return "none";
    case Cause::null_request:
        return "null-request";
    case Cause::not_delete:
        return "not-delete";
    case Cause::flags:
        return "flags";
    case Cause::list_invalid:
        return "list-invalid";
    case Cause::wildcard:
        return "wildcard";
    case Cause::path_too_long:
        return "path-too-long";
    case Cause::ansi_conversion:
        return "ansi-conversion";
    case Cause::outside_root:
        return "outside-root";
    case Cause::stream:
        return "stream";
    }
    return "unknown";
}

bool FlagsHandled(unsigned flags) noexcept {
    return (flags & RequiredFlags) == RequiredFlags &&
           (flags & ~PermittedFlags) == 0;
}

Cause DecideOperation(unsigned function, unsigned flags) noexcept {
    if (function != FoDelete) {
        return Cause::not_delete;
    }
    return FlagsHandled(flags) ? Cause::none : Cause::flags;
}

ListScan ScanList(const char* list) noexcept { return Scan(list); }
ListScan ScanList(const wchar_t* list) noexcept { return Scan(list); }

bool HasWildcard(std::wstring_view path) noexcept {
    for (const wchar_t character : path) {
        if (IsWildcard(character)) {
            return true;
        }
    }
    return false;
}

bool StrictlyUnder(std::wstring_view path, std::wstring_view root) noexcept {
    while (!root.empty() && root.back() == L'\\') {
        root.remove_suffix(1);
    }
    return !root.empty() && path.size() > root.size() + 1 &&
           CompareStringOrdinal(path.data(), int(root.size()), root.data(),
                                int(root.size()), TRUE) == CSTR_EQUAL &&
           path[root.size()] == L'\\' && path[root.size() + 1] != L'\\';
}

bool NamesStream(std::wstring_view path) noexcept {
    if (path.starts_with(L"\\\\?\\")) {
        path.remove_prefix(4);
    }
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (path[index] == L':' && index != 1) {
            return true;
        }
    }
    return false;
}

bool EndOfListing(DWORD error, bool first_query) noexcept {
    return error == ERROR_NO_MORE_FILES ||
           (first_query && error == ERROR_FILE_NOT_FOUND);
}

void DeleteUnderRoot(const wchar_t* target, const wchar_t* root,
                     DeleteTally& tally,
                     const DeleteOptions& options) noexcept {
    try {
        Engine(tally, options).Run(target, root);
    } catch (...) {
        // Memory exhausted: whatever was not deleted stays in place.
        if (tally.error == ERROR_SUCCESS) {
            tally.error = ERROR_NOT_ENOUGH_MEMORY;
        }
    }
}

} // namespace tdur::mod::shell_delete
