#pragma once

// Deletion of a private save folder without the Shell. NEW POLICY,
// never a recovered routine of the game: when the game asks
// SHFileOperation(FO_DELETE) for paths that all lie strictly under the
// session's private save root, the observation DLL deletes them itself, on
// the calling thread, without the Shell. Every other request is forwarded
// unchanged. See docs/evidence/shell_delete_without_shell.md.
//
// This header holds the pure decisions (list reading, flag and operation
// rules, wildcards, streams, prefix comparison, end of a listing) and the
// deletion engine. Neither depends on Detours or on the observation journal,
// so both are tested on their own (tests/shell_delete_core_test.cpp).
#include <windows.h>

#include <cstddef>
#include <string_view>

namespace tdur::mod::shell_delete {

inline constexpr unsigned FoDelete = 3;            // FO_DELETE
inline constexpr std::size_t ListLimit = 32768;    // Characters, in all.
inline constexpr std::size_t PathLimit = MAX_PATH; // Per path, exclusive.
inline constexpr unsigned RequiredFlags = 0x0010u; // FOF_NOCONFIRMATION
// FOF_SILENT (0x4), FOF_NOCONFIRMATION (0x10), FOF_SIMPLEPROGRESS (0x100),
// FOF_NOCONFIRMMKDIR (0x200), FOF_NOERRORUI (0x400) and
// FOF_NOCOPYSECURITYATTRIBS (0x800).
inline constexpr unsigned PermittedFlags =
    0x0004u | 0x0010u | 0x0100u | 0x0200u | 0x0400u | 0x0800u;

// Why a request is forwarded to the original function; none means that the
// DLL handles it.
enum class Cause {
    none,
    null_request,
    not_delete,
    flags,
    list_invalid,
    wildcard,
    path_too_long,
    ansi_conversion,
    outside_root,
    stream,
};

const char* CauseName(Cause cause) noexcept;

// Operation and flag rules: FO_DELETE only, FOF_NOCONFIRMATION required and
// no bit outside PermittedFlags (no recycle bin, no user interface, no name
// mappings, no connected elements, no wildcard options).
bool FlagsHandled(unsigned flags) noexcept;
Cause DecideOperation(unsigned function, unsigned flags) noexcept;

// Bounded reading of a double-NUL-terminated pFrom list. At most ListLimit
// characters are read, the final terminator included. An empty string ends
// the list (as for the Shell), so an empty first entry is an empty list.
struct ListScan {
    Cause cause = Cause::list_invalid;
    std::size_t count = 0;
};
ListScan ScanList(const char* list) noexcept;
ListScan ScanList(const wchar_t* list) noexcept;
bool HasWildcard(std::wstring_view path) noexcept;

// True when `path` names an entry strictly below `root`: case-insensitive
// ordinal prefix, followed by a separator and at least one more character.
// The root itself is not below itself.
bool StrictlyUnder(std::wstring_view path, std::wstring_view root) noexcept;

// True when `path` names a stream: a colon anywhere but right after the
// drive letter (a leading \\?\ is skipped). The hooks forward such a path
// (cause stream) and the engine refuses it.
bool NamesStream(std::wstring_view path) noexcept;

// True when a directory query that failed with `error` only reached the end
// of the listing: ERROR_NO_MORE_FILES, or ERROR_FILE_NOT_FOUND on the first
// query (a file system without "." and ".." answers so for an empty
// directory).
bool EndOfListing(DWORD error, bool first_query) noexcept;

struct DeleteTally {
    DWORD error = ERROR_SUCCESS; // First failure met, 0 when none.
    unsigned files = 0;
    unsigned directories = 0; // Directory reparse points included.
};

struct DeleteOptions {
    unsigned max_depth = 200; // A directory this deep below the target fails.
    // Test seams; the DLL keeps the defaults and never sets them.
    // legacy_disposition takes the fallback taken when a volume refuses
    // FileDispositionInfoEx; share_delete opens every handle with
    // FILE_SHARE_DELETE, which lifts the protection of held directories so
    // that the check of each object on its own handle is tested alone;
    // after_listing runs once the entries of a directory have been read and
    // before any of them is opened; after_chain runs once the root and every
    // directory down to the target's parent are held, before the target is
    // opened, with the final paths of that parent and of the root.
    bool legacy_disposition = false;
    bool share_delete = false;
    void (*after_listing)(const wchar_t* directory, void* context) = nullptr;
    void (*after_chain)(const wchar_t* parent, const wchar_t* root,
                        void* context) = nullptr;
    void* context = nullptr;
};

// Deletes `target` (an absolute DOS path, with or without the \\?\ prefix)
// and, for a directory, everything below it. The root and every directory
// down to the target are held open; every object is opened relative to the
// handle of its parent and verified on its own handle as that very entry,
// strictly under the final path of `root`, free of streams and with a single
// name. No reparse point is ever traversed: one is deleted as a link. Counts
// and the first error accumulate into `tally`; the engine continues after
// the failure of one entry.
void DeleteUnderRoot(const wchar_t* target, const wchar_t* root,
                     DeleteTally& tally,
                     const DeleteOptions& options = {}) noexcept;

} // namespace tdur::mod::shell_delete
