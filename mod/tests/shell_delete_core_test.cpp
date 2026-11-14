// Pure decisions and deletion engine of the Shell-free save folder
// deletion (new policy). The engine runs on synthetic trees that this test
// creates under its own work folder (argument 1, inside the build tree):
// junctions, hard links, a named stream, symbolic links when the system allows
// them, read-only entries, a held file, a file whose access list refuses
// reading, reparse points swapped in and folders renamed during the deletion.
// No game data, no Documents folder and no user profile folder is touched.
#include "tdur/mod/shell_delete.hpp"

#include <sddl.h>
#include <windows.h>
#include <winioctl.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace sd = tdur::mod::shell_delete;

int checks = 0;
int failures = 0;

void Check(bool condition, const char* what, unsigned long value = 0) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL %s (value %lu)\n", what, value);
    }
}

std::wstring work;    // \\?\<work folder>\<run>
std::wstring root;    // work\root
std::wstring outside; // work\outside

std::wstring Join(const std::wstring& base, std::wstring_view leaf) {
    return base + L"\\" + std::wstring(leaf);
}

bool MakeDir(const std::wstring& path) {
    return CreateDirectoryW(path.c_str(), nullptr) != FALSE;
}

// Creates `path` (a file, or a named stream of an existing file) with
// `content`.
bool WriteNew(const std::wstring& path, std::string_view content) {
    const HANDLE file =
        CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const bool ok = WriteFile(file, content.data(), DWORD(content.size()),
                              &written, nullptr) &&
                    written == content.size();
    CloseHandle(file);
    return ok;
}

bool MakeFile(const std::wstring& path, std::string_view content,
              DWORD attributes = FILE_ATTRIBUTE_NORMAL) {
    return WriteNew(path, content) &&
           SetFileAttributesW(path.c_str(), attributes) != FALSE;
}

DWORD Attributes(const std::wstring& path) {
    return GetFileAttributesW(path.c_str());
}

// Times and attributes of `path`, read through a handle limited to
// attributes, which changes none of them.
FILE_BASIC_INFO Times(const std::wstring& path) {
    FILE_BASIC_INFO basic{};
    const HANDLE file = CreateFileW(
        path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        GetFileInformationByHandleEx(file, FileBasicInfo, &basic,
                                     sizeof(basic));
        CloseHandle(file);
    }
    return basic;
}

bool SameTimes(const FILE_BASIC_INFO& before, const FILE_BASIC_INFO& after) {
    return before.ChangeTime.QuadPart != 0 &&
           before.CreationTime.QuadPart == after.CreationTime.QuadPart &&
           before.LastAccessTime.QuadPart == after.LastAccessTime.QuadPart &&
           before.LastWriteTime.QuadPart == after.LastWriteTime.QuadPart &&
           before.ChangeTime.QuadPart == after.ChangeTime.QuadPart &&
           before.FileAttributes == after.FileAttributes;
}

bool Exists(const std::wstring& path) {
    return Attributes(path) != INVALID_FILE_ATTRIBUTES;
}

std::string Content(const std::wstring& path) {
    const HANDLE file =
        CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }
    char buffer[256]{};
    DWORD read = 0;
    ReadFile(file, buffer, sizeof(buffer), &read, nullptr);
    CloseHandle(file);
    return std::string(buffer, read);
}

// Mount point (junction) reparse data, laid out as in the public reparse
// point documentation; the kernel structure is not in the user headers.
bool MakeJunction(const std::wstring& link, const std::wstring& target) {
    if (!MakeDir(link)) {
        return false;
    }
    const std::wstring plain = target.substr(4); // Drop \\?\.
    const std::wstring substitute = L"\\??\\" + plain;
    const std::size_t names =
        (substitute.size() + 1 + plain.size() + 1) * sizeof(wchar_t);
    std::vector<unsigned char> buffer(16 + names);
    auto put16 = [&](std::size_t offset, std::size_t value) {
        buffer[offset] = static_cast<unsigned char>(value & 0xff);
        buffer[offset + 1] = static_cast<unsigned char>((value >> 8) & 0xff);
    };
    const DWORD tag = IO_REPARSE_TAG_MOUNT_POINT;
    std::memcpy(buffer.data(), &tag, sizeof(tag));
    put16(4, 8 + names);
    put16(8, 0);
    put16(10, substitute.size() * sizeof(wchar_t));
    put16(12, (substitute.size() + 1) * sizeof(wchar_t));
    put16(14, plain.size() * sizeof(wchar_t));
    std::memcpy(buffer.data() + 16, substitute.c_str(),
                (substitute.size() + 1) * sizeof(wchar_t));
    std::memcpy(buffer.data() + 16 + (substitute.size() + 1) * sizeof(wchar_t),
                plain.c_str(), (plain.size() + 1) * sizeof(wchar_t));
    const HANDLE handle = CreateFileW(
        link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD returned = 0;
    const bool ok =
        DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(),
                        DWORD(buffer.size()), nullptr, 0, &returned, nullptr);
    CloseHandle(handle);
    return ok;
}

// Removes what a failed case left, never following a reparse point.
void Clean(const std::wstring& path) {
    const DWORD attributes = Attributes(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        return;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
        WIN32_FIND_DATAW data{};
        const HANDLE find = FindFirstFileW(Join(path, L"*").c_str(), &data);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                const std::wstring_view name = data.cFileName;
                if (name != L"." && name != L"..") {
                    Clean(Join(path, name));
                }
            } while (FindNextFileW(find, &data));
            FindClose(find);
        }
    }
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        RemoveDirectoryW(path.c_str());
    } else {
        DeleteFileW(path.c_str());
    }
}

sd::DeleteTally Delete(const std::wstring& target,
                       const sd::DeleteOptions& options = {}) {
    sd::DeleteTally tally;
    sd::DeleteUnderRoot(target.c_str(), root.c_str(), tally, options);
    return tally;
}

// ---- Pure decisions --------------------------------------------------

template <class Char> std::vector<Char> List(std::initializer_list<int> raw) {
    std::vector<Char> list;
    for (const int value : raw) {
        list.push_back(static_cast<Char>(value));
    }
    return list;
}

template <class Char> void ListReading(const char* api) {
    char what[96]{};
    auto label = [&](const char* text) {
        std::snprintf(what, sizeof(what), "%s %s", api, text);
        return what;
    };
    auto scan = [](const std::vector<Char>& list) {
        return sd::ScanList(list.data());
    };
    const auto one = scan(List<Char>({'a', 0, 0}));
    Check(one.cause == sd::Cause::none && one.count == 1, label("one path"));
    const auto two = scan(List<Char>({'a', 0, 'b', 'c', 0, 0}));
    Check(two.cause == sd::Cause::none && two.count == 2, label("two paths"));
    Check(scan(List<Char>({0, 0})).cause == sd::Cause::list_invalid,
          label("empty list"));
    Check(sd::ScanList(static_cast<const Char*>(nullptr)).cause ==
              sd::Cause::list_invalid,
          label("null list"));
    // An empty entry ends the list: what follows is never read as a path.
    const auto middle = scan(List<Char>({'a', 0, 0, 'b', 0, 0}));
    Check(middle.cause == sd::Cause::none && middle.count == 1,
          label("empty entry ends the list"));
    // Unterminated within the bound: exactly ListLimit characters, no NUL.
    std::vector<Char> unterminated(sd::ListLimit, static_cast<Char>('x'));
    Check(scan(unterminated).cause == sd::Cause::list_invalid,
          label("unterminated"));
    // Boundary: 327 entries of 99 characters, then one of k characters;
    // the list holds 32700 + k + 2 characters with its terminators.
    for (const std::size_t k : {std::size_t{66}, std::size_t{67}}) {
        std::vector<Char> bounded;
        for (int index = 0; index < 327; ++index) {
            bounded.insert(bounded.end(), 99, static_cast<Char>('p'));
            bounded.push_back(0);
        }
        bounded.insert(bounded.end(), k, static_cast<Char>('q'));
        bounded.push_back(0);
        bounded.push_back(0);
        const auto result = scan(bounded);
        if (k == 66) {
            Check(result.cause == sd::Cause::none && result.count == 328,
                  label("list of exactly ListLimit characters"));
        } else {
            Check(result.cause == sd::Cause::list_invalid,
                  label("list one character over ListLimit"));
        }
    }
    std::vector<Char> longest(MAX_PATH - 1, static_cast<Char>('l'));
    longest.push_back(0);
    longest.push_back(0);
    Check(scan(longest).cause == sd::Cause::none, label("259 characters"));
    std::vector<Char> tooLong(MAX_PATH, static_cast<Char>('l'));
    tooLong.push_back(0);
    tooLong.push_back(0);
    Check(scan(tooLong).cause == sd::Cause::path_too_long,
          label("260 characters"));
    std::vector<Char> second = List<Char>({'a', 0});
    second.insert(second.end(), MAX_PATH, static_cast<Char>('l'));
    second.push_back(0);
    second.push_back(0);
    Check(scan(second).cause == sd::Cause::path_too_long,
          label("second path too long"));
    Check(scan(List<Char>({'a', '*', 'b', 0, 0})).cause == sd::Cause::wildcard,
          label("star"));
    Check(scan(List<Char>({'a', 0, 'a', '?', 0, 0})).cause ==
              sd::Cause::wildcard,
          label("question mark in the second path"));
}

void Decisions() {
    ListReading<char>("A");
    ListReading<wchar_t>("W");
    Check(sd::HasWildcard(L"C:\\a\\b*") && sd::HasWildcard(L"\\\\?\\C:\\a") &&
              !sd::HasWildcard(L"C:\\a\\b"),
          "wildcard on converted paths");
    for (unsigned bit = 0; bit < 16; ++bit) {
        const unsigned flag = 1u << bit;
        const bool permitted = (sd::PermittedFlags & flag) != 0;
        char what[64]{};
        std::snprintf(what, sizeof(what), "flag bit 0x%04x", flag);
        Check(sd::FlagsHandled(sd::RequiredFlags | flag) == permitted, what,
              flag);
    }
    Check(sd::FlagsHandled(0x0414), "game flags 0x0414 handled");
    Check(!sd::FlagsHandled(0x0404), "confirmation requested");
    Check(!sd::FlagsHandled(0), "no flag at all");
    Check(!sd::FlagsHandled(0x0454), "recycle bin requested");
    Check(sd::DecideOperation(sd::FoDelete, 0x0414) == sd::Cause::none,
          "delete handled");
    for (const unsigned function : {0u, 1u, 2u, 4u, 5u}) {
        Check(sd::DecideOperation(function, 0x0414) == sd::Cause::not_delete,
              "other operations forwarded", function);
    }
    Check(sd::DecideOperation(sd::FoDelete, 0x0454) == sd::Cause::flags,
          "flags forwarded");
    Check(sd::DecideOperation(2, 0x0454) == sd::Cause::not_delete,
          "operation decided before flags");
    Check(std::strcmp(sd::CauseName(sd::Cause::outside_root), "outside-root") ==
                  0 &&
              std::strcmp(sd::CauseName(sd::Cause::not_delete), "not-delete") ==
                  0 &&
              std::strcmp(sd::CauseName(sd::Cause::list_invalid),
                          "list-invalid") == 0 &&
              std::strcmp(sd::CauseName(sd::Cause::path_too_long),
                          "path-too-long") == 0 &&
              std::strcmp(sd::CauseName(sd::Cause::ansi_conversion),
                          "ansi-conversion") == 0 &&
              std::strcmp(sd::CauseName(sd::Cause::null_request),
                          "null-request") == 0 &&
              std::strcmp(sd::CauseName(sd::Cause::wildcard), "wildcard") ==
                  0 &&
              std::strcmp(sd::CauseName(sd::Cause::flags), "flags") == 0,
          "cause names");
    Check(std::strcmp(sd::CauseName(sd::Cause::stream), "stream") == 0,
          "stream cause name");
    // A colon anywhere but right after the drive letter names a stream.
    Check(sd::NamesStream(L"C:\\r\\s:t") &&
              sd::NamesStream(L"C:\\r\\s::$DATA") &&
              sd::NamesStream(L"\\\\?\\C:\\r\\s:t") && sd::NamesStream(L":x") &&
              sd::NamesStream(L"C:\\r:"),
          "stream named");
    Check(!sd::NamesStream(L"C:\\r\\s") && !sd::NamesStream(L"\\\\?\\C:\\r") &&
              !sd::NamesStream(L"C:") && !sd::NamesStream(L""),
          "drive colon only");
    Check(sd::EndOfListing(ERROR_NO_MORE_FILES, true) &&
              sd::EndOfListing(ERROR_NO_MORE_FILES, false),
          "no more files ends a listing");
    // A file system without "." and ".." answers the first query on an
    // empty directory with ERROR_FILE_NOT_FOUND.
    Check(sd::EndOfListing(ERROR_FILE_NOT_FOUND, true),
          "first query without any entry ends a listing");
    Check(!sd::EndOfListing(ERROR_FILE_NOT_FOUND, false) &&
              !sd::EndOfListing(ERROR_ACCESS_DENIED, true),
          "other listing failures stay failures");
    Check(sd::StrictlyUnder(L"C:\\r\\s", L"C:\\r"), "child under root");
    Check(sd::StrictlyUnder(L"c:\\R\\S\\t", L"C:\\r"), "case ignored");
    Check(!sd::StrictlyUnder(L"C:\\r", L"C:\\r"), "root not under itself");
    Check(!sd::StrictlyUnder(L"C:\\r\\", L"C:\\r"), "bare separator");
    Check(!sd::StrictlyUnder(L"C:\\rs", L"C:\\r"), "sibling prefix");
    Check(!sd::StrictlyUnder(L"C:\\q\\s", L"C:\\r"), "other folder");
    Check(!sd::StrictlyUnder(L"C:\\r\\s", L""), "empty root");
    Check(sd::StrictlyUnder(L"C:\\r\\s", L"C:\\r\\"), "root separator");
    Check(sd::StrictlyUnder(L"\\\\?\\C:\\r\\s", L"\\\\?\\C:\\r"),
          "final path form");
    Check(!sd::StrictlyUnder(L"\\\\?\\C:\\r", L"\\\\?\\C:\\r\\s"),
          "parent of root");
}

// ---- Engine ----------------------------------------------------------

void DeepTree() {
    const auto top = Join(root, L"deep");
    auto path = top;
    bool made = MakeDir(path);
    for (int level = 0; level < 50; ++level) {
        wchar_t leaf[8]{};
        std::swprintf(leaf, 8, L"d%02d", level);
        path = Join(path, leaf);
        made = made && MakeDir(path);
    }
    made = made && MakeFile(Join(path, L"leaf.txt"), "deep");
    Check(made, "deep tree created");
    const auto tally = Delete(top);
    Check(tally.error == 0, "deep tree deleted", tally.error);
    Check(tally.directories == 51 && tally.files == 1, "deep tree counts",
          tally.directories);
    Check(!Exists(top) && Exists(root), "deep tree gone, root kept");
}

void ReadOnly(bool legacy) {
    const auto top = Join(root, legacy ? L"ro-legacy" : L"ro");
    const auto sub = Join(top, L"sub");
    const bool made =
        MakeDir(top) &&
        MakeFile(Join(top, L"file.txt"), "ro", FILE_ATTRIBUTE_READONLY) &&
        MakeDir(sub) &&
        MakeFile(Join(sub, L"inner.txt"), "ro", FILE_ATTRIBUTE_READONLY) &&
        SetFileAttributesW(sub.c_str(), FILE_ATTRIBUTE_READONLY);
    Check(made, "read-only tree created");
    sd::DeleteOptions options;
    options.legacy_disposition = legacy;
    const auto tally = Delete(top, options);
    Check(tally.error == 0,
          legacy ? "read-only tree deleted (fallback)" : "read-only deleted",
          tally.error);
    Check(tally.files == 2 && tally.directories == 2, "read-only counts",
          tally.files);
    Check(!Exists(top), "read-only tree gone");
    Clean(top);
}

// Counts the directories the engine listed whose name is `"\link`".
void CountLinkListing(const wchar_t* directory, void* context) {
    const std::wstring_view name = directory;
    if (name.size() >= 5 && name.substr(name.size() - 5) == L"\\link") {
        ++*static_cast<int*>(context);
    }
}

void Junction() {
    const auto target = Join(outside, L"jt");
    const auto witness = Join(target, L"witness.txt");
    const auto top = Join(root, L"jn");
    const DWORD witnessAttributes =
        FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE;
    const bool made = MakeDir(target) &&
                      MakeFile(witness, "outside", witnessAttributes) &&
                      MakeDir(top) && MakeFile(Join(top, L"a.txt"), "a") &&
                      MakeJunction(Join(top, L"link"), target);
    Check(made, "junction tree created");
    int linkListings = 0;
    sd::DeleteOptions options;
    options.after_listing = CountLinkListing;
    options.context = &linkListings;
    const auto tally = Delete(top, options);
    Check(tally.error == 0, "junction tree deleted", tally.error);
    Check(linkListings == 0, "junction never listed",
          static_cast<unsigned long>(linkListings));
    Check(tally.files == 1 && tally.directories == 2, "junction counts",
          tally.directories);
    Check(!Exists(top), "junction removed as a link");
    Check(Exists(target) && Content(witness) == "outside" &&
              Attributes(witness) == witnessAttributes,
          "junction target and its attributes intact", Attributes(witness));
    Clean(top);
}

// An object with more than one name is never deleted, with either
// disposition form: its other names may lie outside the root and share its
// data, attributes, times and streams. The rest of the tree is deleted.
void HardLink(bool legacy) {
    const auto external = Join(outside, legacy ? L"hl-legacy.txt" : L"hl.txt");
    const auto top = Join(root, legacy ? L"hd-legacy" : L"hd");
    const auto link = Join(top, L"link.txt");
    const auto single = Join(top, L"single.txt");
    const bool made =
        MakeFile(external, "linked", FILE_ATTRIBUTE_NORMAL) && MakeDir(top) &&
        CreateHardLinkW(link.c_str(), external.c_str(), nullptr) &&
        SetFileAttributesW(external.c_str(), FILE_ATTRIBUTE_READONLY) &&
        MakeFile(single, "single", FILE_ATTRIBUTE_READONLY);
    Check(made, "hard link tree created");
    sd::DeleteOptions options;
    options.legacy_disposition = legacy;
    const auto before = Times(external);
    const auto tally = Delete(top, options);
    const auto after = Times(external);
    Check(tally.error == ERROR_ACCESS_DENIED,
          legacy ? "fallback: hard link refused" : "hard link refused",
          tally.error);
    Check(Exists(link) && !Exists(single) && Exists(top),
          legacy ? "fallback: link kept, single read-only file deleted"
                 : "link kept, single read-only file deleted");
    Check(SameTimes(before, after), legacy
                                        ? "fallback: external file times intact"
                                        : "external file times intact");
    Check(Content(external) == "linked" &&
              Attributes(external) == FILE_ATTRIBUTE_READONLY,
          legacy ? "fallback: external file and attributes intact"
                 : "external file and attributes intact",
          Attributes(external));
    Clean(top);
}

// A named stream under the root, on a hard link to an outside file: the
// stream belongs to the outside file too. The request is refused before
// anything is opened, and the stream, the data, the attributes and the
// times of the outside file stay as they were.
void StreamOnLink() {
    const auto external = Join(outside, L"streamed.txt");
    const auto top = Join(root, L"st");
    const auto link = Join(top, L"lien.sav");
    // The attribute is set last: creating a hard link sets the archive one.
    const bool made =
        WriteNew(external, "data") && WriteNew(external + L":flux", "zone") &&
        MakeDir(top) &&
        CreateHardLinkW(link.c_str(), external.c_str(), nullptr) &&
        SetFileAttributesW(external.c_str(), FILE_ATTRIBUTE_READONLY);
    Check(made, "stream link fixture created");
    const auto before = Times(external);
    const auto tally = Delete(link + L":flux");
    const auto after = Times(external);
    Check(tally.error == ERROR_INVALID_NAME && tally.files == 0 &&
              tally.directories == 0,
          "stream on a hard link refused", tally.error);
    Check(SameTimes(before, after), "outside streamed file times intact");
    Check(Content(external + L":flux") == "zone", "outside stream intact");
    Check(Content(external) == "data" &&
              Attributes(external) == FILE_ATTRIBUTE_READONLY && Exists(link),
          "outside streamed file and link intact", Attributes(external));
    Clean(top);
}

void SymbolicLinks() {
    const auto file = Join(outside, L"sf.txt");
    const auto folder = Join(outside, L"sd");
    const auto inner = Join(folder, L"w.txt");
    const auto top = Join(root, L"sy");
    bool made = MakeFile(file, "symfile", FILE_ATTRIBUTE_READONLY) &&
                MakeDir(folder) && MakeFile(inner, "symdir") && MakeDir(top);
    Check(made, "symbolic link fixtures created");
    constexpr DWORD unprivileged = 0x2; // ALLOW_UNPRIVILEGED_CREATE
    const bool fileLink = CreateSymbolicLinkW(Join(top, L"flink").c_str(),
                                              file.c_str(), unprivileged);
    const DWORD fileError = fileLink ? 0 : GetLastError();
    const bool folderLink =
        CreateSymbolicLinkW(Join(top, L"dlink").c_str(), folder.c_str(),
                            SYMBOLIC_LINK_FLAG_DIRECTORY | unprivileged);
    if (!fileLink || !folderLink) {
        std::printf(
            "symbolic links unavailable on this system (error %lu); "
            "case skipped\n",
            static_cast<unsigned long>(fileError ? fileError : GetLastError()));
        Clean(top);
        return;
    }
    std::printf("symbolic links created and checked\n");
    const auto tally = Delete(top);
    Check(tally.error == 0, "symbolic links deleted", tally.error);
    Check(!Exists(top), "symbolic links gone");
    Check(Content(file) == "symfile" &&
              Attributes(file) == FILE_ATTRIBUTE_READONLY &&
              Content(inner) == "symdir",
          "symbolic link targets intact");
    Clean(top);
}

void Absent() {
    const auto missing = Delete(Join(root, L"absent"));
    Check(missing.error == ERROR_FILE_NOT_FOUND && missing.files == 0 &&
              missing.directories == 0,
          "absent target", missing.error);
    const auto parent = Delete(Join(root, L"nodir\\x"));
    Check(parent.error == ERROR_PATH_NOT_FOUND, "absent parent", parent.error);
}

void FileTarget() {
    const auto file = Join(root, L"single.txt");
    Check(MakeFile(file, "one"), "file target created");
    const auto tally = Delete(file);
    Check(tally.error == 0 && tally.files == 1 && tally.directories == 0,
          "file target deleted", tally.error);
    Check(!Exists(file), "file target gone");
}

void OutsideRoot() {
    const auto keep = Join(outside, L"keep");
    const auto inner = Join(keep, L"k.txt");
    Check(MakeDir(keep) && MakeFile(inner, "keep"), "outside fixture created");
    const auto tally = Delete(keep);
    Check(tally.error == ERROR_ACCESS_DENIED && tally.files == 0 &&
              tally.directories == 0,
          "outside target refused", tally.error);
    Check(Content(inner) == "keep", "outside target intact");
    const auto self = Delete(root);
    Check(self.error == ERROR_ACCESS_DENIED, "root itself refused", self.error);
    Check(Exists(Join(root, L"root-canary.txt")), "root content intact");
}

void ThroughJunction() {
    const auto target = Join(outside, L"vt");
    const auto inner = Join(target, L"inner");
    const auto witness = Join(inner, L"w.txt");
    const auto via = Join(root, L"via");
    const bool made = MakeDir(target) && MakeDir(inner) &&
                      MakeFile(witness, "via") && MakeJunction(via, target);
    Check(made, "junction path fixture created");
    // The name lies under the root, the object opened through the junction
    // does not: the check by handle refuses it.
    const auto tally = Delete(Join(via, L"inner"));
    Check(tally.error == ERROR_ACCESS_DENIED && tally.files == 0 &&
              tally.directories == 0,
          "target reached through a junction refused", tally.error);
    Check(Content(witness) == "via", "object behind the junction intact");
    RemoveDirectoryW(via.c_str());
}

void RootThroughJunction() {
    const auto alias = Join(work, L"alias");
    const auto victim = Join(root, L"aliased");
    const auto inner = Join(victim, L"a.txt");
    const bool made =
        MakeJunction(alias, root) && MakeDir(victim) && MakeFile(inner, "a");
    Check(made, "root alias fixture created");
    // The root named through a junction is elsewhere than its name says.
    sd::DeleteTally tally;
    sd::DeleteUnderRoot(Join(alias, L"aliased").c_str(), alias.c_str(), tally);
    Check(tally.error == ERROR_ACCESS_DENIED && tally.files == 0,
          "root reached through a junction refused", tally.error);
    Check(Content(inner) == "a", "object under the aliased root intact");
    // A folder above the root is a junction: the root named through it is a
    // plain directory, whose final path differs from its name.
    const auto up = Join(outside, L"up");
    Check(MakeJunction(up, work), "junction above the root created");
    sd::DeleteTally above;
    sd::DeleteUnderRoot(Join(up, L"root\\aliased").c_str(),
                        Join(up, L"root").c_str(), above);
    Check(above.error == ERROR_ACCESS_DENIED && above.files == 0,
          "root below a junction refused", above.error);
    Check(Content(inner) == "a",
          "object under the root below a junction intact");
    // The same root named below the junction, the target named by its own
    // final path: only the check of the root's final path refuses it.
    sd::DeleteTally crossed;
    sd::DeleteUnderRoot(Join(root, L"aliased").c_str(),
                        Join(up, L"root").c_str(), crossed);
    Check(crossed.error == ERROR_ACCESS_DENIED && crossed.files == 0,
          "root below a junction refused for a target named by its real path",
          crossed.error);
    Check(Content(inner) == "a",
          "object under the root below a junction intact, real target path");
    RemoveDirectoryW(up.c_str());
    RemoveDirectoryW(alias.c_str());
    Clean(victim);
}

void DeeperTarget() {
    const auto chain = Join(root, L"chain");
    const auto inner = Join(chain, L"inner");
    const auto target = Join(inner, L"t");
    const bool made = MakeDir(chain) && MakeDir(inner) && MakeDir(target) &&
                      MakeFile(Join(target, L"f.txt"), "f");
    Check(made, "deeper target created");
    const auto tally = Delete(target);
    Check(tally.error == 0 && tally.files == 1 && tally.directories == 1,
          "deeper target deleted", tally.error);
    Check(!Exists(target) && Exists(inner), "deeper target gone, chain kept");
    Clean(chain);
}

void Partial() {
    const auto top = Join(root, L"part");
    const auto held = Join(top, L"held.txt");
    const auto sub = Join(top, L"b");
    const bool made = MakeDir(top) && MakeFile(Join(top, L"a.txt"), "a") &&
                      MakeFile(held, "held") && MakeDir(sub) &&
                      MakeFile(Join(sub, L"c.txt"), "c");
    Check(made, "partial fixture created");
    const HANDLE hold =
        CreateFileW(held.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(hold != INVALID_HANDLE_VALUE, "held file opened");
    const auto tally = Delete(top);
    if (hold != INVALID_HANDLE_VALUE) {
        CloseHandle(hold);
    }
    Check(tally.error == ERROR_SHARING_VIOLATION, "held file reported",
          tally.error);
    Check(tally.files == 2 && tally.directories == 1, "partial counts",
          tally.files);
    Check(!Exists(Join(top, L"a.txt")) && !Exists(sub) &&
              Content(held) == "held" && Exists(top),
          "the rest deleted, the held file intact");
    Clean(top);
}

struct Swap {
    std::wstring sub;
    std::wstring target;
    bool fired = false;
    bool made = false;
};

void SwapAfterListing(const wchar_t* directory, void* context) {
    auto& swap = *static_cast<Swap*>(context);
    const std::wstring_view name = directory;
    if (swap.fired || name.size() < 5 ||
        name.substr(name.size() - 5) != L"\\race") {
        return;
    }
    swap.fired = true;
    swap.made = RemoveDirectoryW(swap.sub.c_str()) &&
                MakeJunction(swap.sub, swap.target);
}

void SwapDuringDeletion() {
    const auto top = Join(root, L"race");
    Swap swap;
    swap.sub = Join(top, L"sub");
    swap.target = Join(outside, L"rt");
    const auto witness = Join(swap.target, L"w.txt");
    const bool made = MakeDir(top) && MakeDir(swap.sub) &&
                      MakeFile(Join(top, L"z.txt"), "z") &&
                      MakeDir(swap.target) && MakeFile(witness, "race");
    Check(made, "swap fixture created");
    sd::DeleteOptions options;
    options.after_listing = SwapAfterListing;
    options.context = &swap;
    const auto tally = Delete(top, options);
    Check(swap.fired && swap.made, "directory swapped for a junction");
    Check(tally.error == 0, "swapped tree deleted", tally.error);
    Check(!Exists(top), "swapped junction removed as a link");
    Check(Content(witness) == "race", "swapped junction target intact");
    Clean(top);
}

struct Move {
    std::wstring moved;  // Where the listed directory is renamed to.
    std::wstring target; // Outside folder the junction points to.
    bool fired = false;
    bool renamed = false;
    DWORD error = 0;
    bool junction = false;
};

// Tries to move the directory just listed away and to put a junction to an
// outside folder in its place, whose entry has the same name as the one
// listed.
void MoveAfterListing(const wchar_t* directory, void* context) {
    auto& move = *static_cast<Move*>(context);
    const std::wstring_view name = directory;
    if (move.fired || name.size() < 5 ||
        name.substr(name.size() - 5) != L"\\held") {
        return;
    }
    move.fired = true;
    move.renamed = MoveFileExW(directory, move.moved.c_str(), 0) != FALSE;
    move.error = move.renamed ? 0 : GetLastError();
    if (move.renamed) {
        move.junction = MakeJunction(directory, move.target);
    }
}

// The engine holds every directory of the branch without FILE_SHARE_DELETE:
// the directory being emptied cannot be moved away under it.
void HeldDirectory() {
    const auto top = Join(root, L"held");
    const auto victim = Join(top, L"victim.txt");
    Move move;
    move.moved = Join(root, L"held-moved");
    move.target = Join(outside, L"ht");
    const bool made = MakeDir(top) && MakeFile(victim, "inside") &&
                      MakeDir(move.target) &&
                      MakeFile(Join(move.target, L"victim.txt"), "outside");
    Check(made, "held directory fixture created");
    sd::DeleteOptions options;
    options.after_listing = MoveAfterListing;
    options.context = &move;
    const auto tally = Delete(top, options);
    Check(move.fired && !move.renamed &&
              (move.error == ERROR_SHARING_VIOLATION ||
               move.error == ERROR_ACCESS_DENIED),
          "held directory not movable during the deletion", move.error);
    Check(tally.error == 0 && !Exists(top), "held directory deleted",
          tally.error);
    Check(Content(Join(move.target, L"victim.txt")) == "outside",
          "outside folder intact");
    if (move.junction) {
        RemoveDirectoryW(top.c_str());
    }
    Clean(move.moved);
    Clean(top);
}

// Same move with the sharing protection lifted by the test option: the
// directory is moved away and replaced by a junction to an outside folder
// whose entry has the same name and is held open by the test without
// sharing deletion. Every entry is opened relative to the handle of the
// directory listed, so the junction is never traversed (an open by path
// would reach the held decoy and meet a sharing violation): the entry
// reached is the one of the moved directory, refused on its own handle.
void MovedDirectory() {
    const auto top = Join(root, L"held");
    const auto victim = Join(top, L"victim.txt");
    Move move;
    move.moved = Join(root, L"held-moved");
    move.target = Join(outside, L"mt");
    const auto decoy = Join(move.target, L"victim.txt");
    const bool made = MakeDir(top) && MakeFile(victim, "inside") &&
                      MakeDir(move.target) && MakeFile(decoy, "outside");
    Check(made, "moved directory fixture created");
    const HANDLE hold =
        CreateFileW(decoy.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(hold != INVALID_HANDLE_VALUE, "decoy held");
    sd::DeleteOptions options;
    options.after_listing = MoveAfterListing;
    options.context = &move;
    options.share_delete = true;
    const auto tally = Delete(top, options);
    if (hold != INVALID_HANDLE_VALUE) {
        CloseHandle(hold);
    }
    Check(move.fired && move.renamed && move.junction,
          "directory moved and replaced by a junction", move.error);
    Check(tally.error == ERROR_ACCESS_DENIED,
          "entry behind the junction refused", tally.error);
    Check(Content(decoy) == "outside", "outside entry intact");
    Check(Content(Join(move.moved, L"victim.txt")) == "inside",
          "moved directory left as it was");
    if (move.junction) {
        RemoveDirectoryW(top.c_str());
    }
    Clean(move.moved);
    Clean(top);
}

struct Renames {
    std::wstring parent; // Expected final path of the target's parent.
    bool fired = false;
    bool named = false;
    DWORD parentError = 0; // 0 when the rename succeeded.
    DWORD rootError = 0;
};

// Renames `from` to `from`-moved; returns 0 on success (and moves it back at
// once, so that a failure here does not break the cases that follow), the
// error otherwise.
DWORD TryRename(const std::wstring& from) {
    const std::wstring to = from + L"-moved";
    if (!MoveFileExW(from.c_str(), to.c_str(), 0)) {
        const DWORD error = GetLastError();
        return error != 0 ? error : ERROR_GEN_FAILURE;
    }
    MoveFileExW(to.c_str(), from.c_str(), 0);
    return 0;
}

// Runs once the engine holds the root and every directory down to the
// target's parent: both renames must fail.
void RenameAfterChain(const wchar_t* parent, const wchar_t* heldRoot,
                      void* context) {
    auto& renames = *static_cast<Renames*>(context);
    renames.fired = true;
    renames.named = CompareStringOrdinal(parent, -1, renames.parent.c_str(), -1,
                                         TRUE) == CSTR_EQUAL;
    renames.parentError = TryRename(parent);
    renames.rootError = TryRename(heldRoot);
}

// The root and the directories between the root and the target are held
// with FILE_LIST_DIRECTORY and without FILE_SHARE_DELETE: they cannot be
// renamed while the engine works, and the deletion proceeds.
void HeldChain() {
    const auto first = Join(root, L"hc");
    const auto parent = Join(first, L"inner");
    const auto target = Join(parent, L"t");
    const bool made = MakeDir(first) && MakeDir(parent) && MakeDir(target) &&
                      MakeFile(Join(target, L"f.txt"), "f");
    Check(made, "held chain fixture created");
    Renames renames;
    renames.parent = parent;
    sd::DeleteOptions options;
    options.after_chain = RenameAfterChain;
    options.context = &renames;
    const auto tally = Delete(target, options);
    Check(renames.fired && renames.named,
          "chain callback reached with the target's parent");
    Check(renames.parentError != 0,
          "parent of the target not renamable during the deletion",
          renames.parentError);
    Check(renames.rootError != 0, "root not renamable during the deletion",
          renames.rootError);
    Check(tally.error == 0 && tally.files == 1 && tally.directories == 1,
          "target below the held chain deleted", tally.error);
    Check(!Exists(target) && Exists(parent) && Exists(root),
          "held chain kept, target gone");
    Clean(first);
}

// A component of the target is a junction to another folder of the root:
// the probe reaches an object whose final path differs from the name
// requested, and the request is refused; nothing behind the junction is
// deleted in place of the target named.
void JunctionInsideRoot() {
    const auto real = Join(root, L"jb");
    const auto victim = Join(real, L"victim.txt");
    const auto alias = Join(root, L"ja");
    const bool made = MakeDir(real) && MakeFile(victim, "inside") &&
                      MakeJunction(alias, real);
    Check(made, "junction inside the root created");
    const auto tally = Delete(Join(alias, L"victim.txt"));
    Check(tally.error == ERROR_ACCESS_DENIED && tally.files == 0,
          "target named through an inner junction refused", tally.error);
    Check(Content(victim) == "inside" && Exists(alias),
          "object behind the inner junction intact");
    RemoveDirectoryW(alias.c_str());
    Clean(real);
}

// The target itself is a junction to an outside folder: it is deleted as a
// link and the folder it points to stays as it was.
void JunctionTarget() {
    const auto pointed = Join(outside, L"jtt");
    const auto inner = Join(pointed, L"w.txt");
    const auto link = Join(root, L"jl");
    const bool made = MakeDir(pointed) && MakeFile(inner, "pointed") &&
                      MakeJunction(link, pointed);
    Check(made, "junction target created");
    const auto tally = Delete(link);
    Check(tally.error == 0 && tally.directories == 1 && tally.files == 0,
          "junction target deleted as a link", tally.error);
    Check(!Exists(link) && Content(inner) == "pointed",
          "junction target gone, folder behind intact");
    RemoveDirectoryW(link.c_str());
}

// A file whose access list grants only READ_CONTROL, SYNCHRONIZE and
// FILE_READ_ATTRIBUTES, in a folder of the test: its deletion asks only for
// DELETE (granted through FILE_DELETE_CHILD on the folder) and
// FILE_READ_ATTRIBUTES, never for reading or changing attributes.
void RestrictedFile() {
    const auto top = Join(root, L"acl");
    const auto file = Join(top, L"locked.txt");
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const bool made =
        MakeDir(top) && MakeFile(file, "locked") &&
        ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(A;;0x00120080;;;WD)", SDDL_REVISION_1, &descriptor,
            nullptr) &&
        SetFileSecurityW(file.c_str(), DACL_SECURITY_INFORMATION, descriptor);
    if (descriptor != nullptr) {
        LocalFree(descriptor);
    }
    Check(made, "restricted file created");
    const HANDLE reader =
        CreateFileW(file.c_str(), FILE_READ_DATA, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    const DWORD readError = reader == INVALID_HANDLE_VALUE ? GetLastError() : 0;
    if (reader != INVALID_HANDLE_VALUE) {
        CloseHandle(reader);
    }
    Check(readError == ERROR_ACCESS_DENIED, "restricted file refuses reading",
          readError);
    const auto tally = Delete(top);
    Check(tally.error == 0 && tally.files == 1 && tally.directories == 1,
          "restricted file deleted through the folder's rights", tally.error);
    Check(!Exists(top), "restricted folder gone");
    Clean(top);
}

void Depth() {
    const auto top = Join(root, L"depth");
    auto path = top;
    bool made = MakeDir(path);
    std::wstring fifth;
    for (int level = 0; level < 8; ++level) {
        wchar_t leaf[8]{};
        std::swprintf(leaf, 8, L"l%d", level);
        path = Join(path, leaf);
        made = made && MakeDir(path);
        if (level == 4) {
            fifth = path;
        }
    }
    const auto file = Join(path, L"f.txt");
    made = made && MakeFile(file, "f");
    Check(made, "depth fixture created");
    sd::DeleteOptions options;
    options.max_depth = 5;
    const auto tally = Delete(top, options);
    Check(tally.error == ERROR_FILENAME_EXCED_RANGE, "depth limit reported",
          tally.error);
    Check(Content(file) == "f" && Exists(fifth) && Exists(top),
          "nothing deleted below the depth limit");
    Clean(top);
}
} // namespace

int main(int argc, char** argv) {
    Decisions();
    if (argc != 2) {
        std::printf("usage: tdur_shell_delete_core_test <work folder>\n");
        return 2;
    }
    wchar_t requested[MAX_PATH]{};
    wchar_t full[MAX_PATH]{};
    if (MultiByteToWideChar(CP_ACP, 0, argv[1], -1, requested, MAX_PATH) == 0 ||
        GetFullPathNameW(requested, MAX_PATH, full, nullptr) == 0) {
        std::printf("FAIL work folder\n");
        return 1;
    }
    CreateDirectoryW(full, nullptr);
    wchar_t run[48]{};
    std::swprintf(run, 48, L"run-%lu-%llu",
                  static_cast<unsigned long>(GetCurrentProcessId()),
                  static_cast<unsigned long long>(GetTickCount64()));
    work = Join(std::wstring(L"\\\\?\\") + full, run);
    root = Join(work, L"root");
    outside = Join(work, L"outside");
    if (!MakeDir(work) || !MakeDir(root) || !MakeDir(outside) ||
        !MakeFile(Join(root, L"root-canary.txt"), "canary")) {
        std::printf("FAIL work tree\n");
        return 1;
    }
    DeepTree();
    ReadOnly(false);
    ReadOnly(true);
    Junction();
    HardLink(false);
    HardLink(true);
    StreamOnLink();
    SymbolicLinks();
    Absent();
    FileTarget();
    OutsideRoot();
    ThroughJunction();
    RootThroughJunction();
    DeeperTarget();
    Partial();
    SwapDuringDeletion();
    HeldDirectory();
    MovedDirectory();
    HeldChain();
    JunctionInsideRoot();
    JunctionTarget();
    RestrictedFile();
    Depth();
    if (failures == 0) {
        Clean(work);
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
