// Out-of-process minidump writer started by the owned launcher, for
// the launcher's own child process only. It receives inherited handles: the
// target process (query and read, without terminate, write or duplicate
// rights), the dump file and the summary file. It opens no other process
// and no other file, writes nothing to the registry and creates no process.
// dbghelp comes from System32 and no symbol is ever loaded
// (SYMOPT_IGNORE_CVREC, no Sym*Initialize): build PDBs stay unlocked.
// clang-format off
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <winternl.h>
// clang-format on

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int ExitDumped = 0;
constexpr int ExitArguments = 10;
constexpr int ExitIdentity = 11;
constexpr int ExitDbghelp = 12;
constexpr int ExitDumpFailed = 13;
constexpr int ExitSummary = 14;

// The dump never contains the full memory, data segments, memory referenced
// indirectly from stacks (code bytes of the game image) nor handle data.
constexpr auto DumpType = static_cast<MINIDUMP_TYPE>(
    MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules |
    MiniDumpWithFullMemoryInfo | MiniDumpWithProcessThreadData |
    MiniDumpIgnoreInaccessibleMemory);

using WriteDump = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                PMINIDUMP_EXCEPTION_INFORMATION,
                                PMINIDUMP_USER_STREAM_INFORMATION,
                                PMINIDUMP_CALLBACK_INFORMATION);
using GetOptions = DWORD(WINAPI*)();
using SetOptions = DWORD(WINAPI*)(DWORD);
using VersionSize = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
using VersionInfo = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
using VersionQuery = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
using QueryProcess = NTSTATUS(NTAPI*)(HANDLE, PROCESSINFOCLASS, PVOID, ULONG,
                                      PULONG);

struct Dbghelp {
    HMODULE module = nullptr;
    WriteDump write = nullptr;
    DWORD options = 0;
    std::string path;
    std::string version;
};

std::string Utf8(std::wstring_view text) {
    if (text.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr,
                                         0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(std::max(size, 0)), '\0');
    if (size > 0)
        WideCharToMultiByte(CP_UTF8, 0, text.data(),
                            static_cast<int>(text.size()), result.data(), size,
                            nullptr, nullptr);
    return result;
}

std::string Json(std::string_view text) {
    std::string result = "\"";
    for (const unsigned char value : text) {
        if (value == '"' || value == '\\') {
            result += '\\';
            result += static_cast<char>(value);
        } else if (value < 32) {
            char escaped[8]{};
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", value);
            result += escaped;
        } else {
            result += static_cast<char>(value);
        }
    }
    return result + "\"";
}

std::string Hex(std::uint64_t value) {
    char text[24]{};
    std::snprintf(text, sizeof(text), "\"0x%llx\"",
                  static_cast<unsigned long long>(value));
    return text;
}

// Version resource of a loaded System32 library. version.dll is loaded from
// System32 too: no static import.
std::string FileVersion(HMODULE module) {
    std::array<wchar_t, MAX_PATH> path{};
    if (GetModuleFileNameW(module, path.data(), MAX_PATH) == 0)
        return {};
    const HMODULE version =
        LoadLibraryExW(L"version.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (version == nullptr)
        return {};
    const auto size = std::bit_cast<VersionSize>(
        GetProcAddress(version, "GetFileVersionInfoSizeW"));
    const auto info = std::bit_cast<VersionInfo>(
        GetProcAddress(version, "GetFileVersionInfoW"));
    const auto query =
        std::bit_cast<VersionQuery>(GetProcAddress(version, "VerQueryValueW"));
    std::string result;
    DWORD ignored = 0;
    const DWORD bytes = size && info && query ? size(path.data(), &ignored) : 0;
    if (bytes != 0) {
        std::vector<std::uint8_t> data(bytes);
        VS_FIXEDFILEINFO* fixed = nullptr;
        UINT length = 0;
        if (info(path.data(), 0, bytes, data.data()) &&
            query(data.data(), L"\\", reinterpret_cast<void**>(&fixed),
                  &length) &&
            fixed != nullptr && length >= sizeof(VS_FIXEDFILEINFO)) {
            char text[64]{};
            std::snprintf(
                text, sizeof(text), "%u.%u.%u.%u",
                HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS),
                HIWORD(fixed->dwFileVersionLS), LOWORD(fixed->dwFileVersionLS));
            result = text;
        }
    }
    FreeLibrary(version);
    return result;
}

bool LoadDbghelp(Dbghelp& result) {
    result.module =
        LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (result.module == nullptr)
        return false;
    result.write = std::bit_cast<WriteDump>(
        GetProcAddress(result.module, "MiniDumpWriteDump"));
    const auto get = std::bit_cast<GetOptions>(
        GetProcAddress(result.module, "SymGetOptions"));
    const auto set = std::bit_cast<SetOptions>(
        GetProcAddress(result.module, "SymSetOptions"));
    if (result.write == nullptr || get == nullptr || set == nullptr)
        return false;
    // Never SymInitialize nor any other Sym* call: no PDB is ever opened.
    set(get() | SYMOPT_IGNORE_CVREC | SYMOPT_DEFERRED_LOADS);
    result.options = get();
    std::array<wchar_t, MAX_PATH> path{};
    if (GetModuleFileNameW(result.module, path.data(), MAX_PATH) != 0)
        result.path = Utf8(path.data());
    result.version = FileVersion(result.module);
    return true;
}

bool ParseUnsigned(std::wstring_view text, std::uint64_t& value) {
    std::uint64_t base = 10;
    if (text.size() > 2 && text[0] == L'0' &&
        (text[1] == L'x' || text[1] == L'X')) {
        base = 16;
        text.remove_prefix(2);
    }
    if (text.empty() || text.size() > 20)
        return false;
    std::uint64_t result = 0;
    for (const wchar_t character : text) {
        std::uint64_t digit = 0;
        if (character >= L'0' && character <= L'9')
            digit = character - L'0';
        else if (base == 16 && character >= L'a' && character <= L'f')
            digit = character - L'a' + 10;
        else if (base == 16 && character >= L'A' && character <= L'F')
            digit = character - L'A' + 10;
        else
            return false;
        if (result > (UINT64_MAX - digit) / base)
            return false;
        result = result * base + digit;
    }
    value = result;
    return true;
}

struct Arguments {
    bool self_check = false;
    bool crash = false;
    HANDLE process = nullptr;
    HANDLE dump = nullptr;
    HANDLE summary = nullptr;
    DWORD pid = 0;
    std::uint64_t creation = 0;
    DWORD thread = 0;
    std::uint32_t pointers = 0;
};

bool Parse(int argc, wchar_t* argv[], Arguments& result) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--self-check") {
        result.self_check = true;
        return true;
    }
    if (argc < 2 || argc % 2 == 0)
        return false;
    const std::set<std::wstring_view> allowed{L"--kind",
                                              L"--process",
                                              L"--dump",
                                              L"--summary",
                                              L"--pid",
                                              L"--thread",
                                              L"--creation-filetime",
                                              L"--exception-pointers"};
    std::map<std::wstring_view, std::wstring_view> values;
    for (int index = 1; index + 1 < argc; index += 2)
        if (!allowed.contains(argv[index]) ||
            !values.emplace(argv[index], argv[index + 1]).second)
            return false;
    for (const auto* required :
         {L"--kind", L"--process", L"--dump", L"--summary", L"--pid",
          L"--creation-filetime"})
        if (!values.contains(required))
            return false;
    const auto kind = values.at(L"--kind");
    if (kind != L"crash" && kind != L"hang")
        return false;
    result.crash = kind == L"crash";
    const bool thread = values.contains(L"--thread");
    const bool pointers = values.contains(L"--exception-pointers");
    if (thread != pointers || thread != result.crash)
        return false;
    const auto number = [&](const wchar_t* name, std::uint64_t limit,
                            std::uint64_t& out) {
        return ParseUnsigned(values.at(name), out) && out != 0 && out <= limit;
    };
    std::uint64_t value = 0;
    const auto handle = [&](const wchar_t* name, HANDLE& out) {
        if (!number(name, UINT32_MAX, value))
            return false;
        out = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(value));
        return true;
    };
    if (!handle(L"--process", result.process) ||
        !handle(L"--dump", result.dump) ||
        !handle(L"--summary", result.summary) ||
        !number(L"--pid", UINT32_MAX, value))
        return false;
    result.pid = static_cast<DWORD>(value);
    if (!number(L"--creation-filetime", UINT64_MAX, result.creation))
        return false;
    if (result.crash) {
        if (!number(L"--thread", UINT32_MAX, value))
            return false;
        result.thread = static_cast<DWORD>(value);
        if (!number(L"--exception-pointers", UINT32_MAX, value))
            return false;
        result.pointers = static_cast<std::uint32_t>(value);
    }
    return true;
}

bool EmptyDiskFile(HANDLE file) {
    LARGE_INTEGER size{};
    return GetFileType(file) == FILE_TYPE_DISK && GetFileSizeEx(file, &size) &&
           size.QuadPart == 0;
}

// Nothing is written unless the inherited handles designate exactly the
// expected live process and two empty disk files.
bool IdentityMatches(const Arguments& value) {
    if (GetProcessId(value.process) != value.pid)
        return false;
    FILETIME creation{}, exit_time{}, kernel_time{}, user_time{};
    if (!GetProcessTimes(value.process, &creation, &exit_time, &kernel_time,
                         &user_time))
        return false;
    const auto created =
        (std::uint64_t(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
    return created == value.creation &&
           WaitForSingleObject(value.process, 0) == WAIT_TIMEOUT &&
           EmptyDiskFile(value.dump) && EmptyDiskFile(value.summary);
}

struct Range {
    std::uint64_t base = 0;
    std::uint32_t size = 0;
    std::string why;
};

std::string_view RegionKind(const MEMORY_BASIC_INFORMATION& info) {
    if (info.State == MEM_FREE)
        return "free";
    if (info.State == MEM_RESERVE)
        return "reserved";
    if (info.Type == MEM_IMAGE)
        return "image";
    return info.Type == MEM_MAPPED ? "mapped" : "private";
}

bool Readable(const MEMORY_BASIC_INFORMATION& info) {
    constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                               PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                               PAGE_EXECUTE_WRITECOPY;
    return info.State == MEM_COMMIT && (info.Protect & PAGE_GUARD) == 0 &&
           (info.Protect & readable) != 0;
}

struct Collector {
    HANDLE process = nullptr;
    std::uint64_t image_base = 0;
    std::uint64_t image_end = 0;
    std::vector<Range> ranges;
    std::size_t next = 0;
    std::vector<std::string> notes;

    bool query(std::uint64_t address, MEMORY_BASIC_INFORMATION& info) const {
        return address <= UINT32_MAX &&
               VirtualQueryEx(process,
                              reinterpret_cast<LPCVOID>(
                                  static_cast<std::uintptr_t>(address)),
                              &info, sizeof(info)) == sizeof(info);
    }
    bool in_image(std::uint64_t begin, std::uint64_t end) const {
        return image_end > image_base && begin < image_end && end > image_base;
    }
    template <typename T> bool read(std::uint64_t address, T& out) const {
        SIZE_T done = 0;
        return address != 0 && address <= UINT32_MAX - sizeof(T) &&
               ReadProcessMemory(process,
                                 reinterpret_cast<LPCVOID>(
                                     static_cast<std::uintptr_t>(address)),
                                 &out, sizeof(T), &done) &&
               done == sizeof(T);
    }
    // Adds [anchor - before, anchor + after) clipped to the committed
    // readable region holding anchor. Never inside the game image, and
    // never an image region at all when private_only is set.
    void add(std::uint64_t anchor, std::uint32_t before, std::uint32_t after,
             bool private_only, std::string_view why) {
        MEMORY_BASIC_INFORMATION info{};
        if (anchor == 0 || !query(anchor, info) || !Readable(info)) {
            notes.push_back(std::string(why) + ":unreadable");
            return;
        }
        if (private_only && info.Type == MEM_IMAGE) {
            notes.push_back(std::string(why) + ":image-region-skipped");
            return;
        }
        const auto region = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        const std::uint64_t region_end = region + info.RegionSize;
        const std::uint64_t begin = std::max<std::uint64_t>(
            anchor >= before ? anchor - before : 0, region);
        const std::uint64_t end =
            std::min<std::uint64_t>(anchor + after, region_end);
        if (end <= begin || in_image(begin, end)) {
            notes.push_back(std::string(why) + ":game-image-skipped");
            return;
        }
        ranges.push_back(
            {begin, static_cast<std::uint32_t>(end - begin), std::string(why)});
    }
};

BOOL CALLBACK DumpCallback(PVOID parameter,
                           const PMINIDUMP_CALLBACK_INPUT input,
                           PMINIDUMP_CALLBACK_OUTPUT output) {
    auto* collector = static_cast<Collector*>(parameter);
    if (input == nullptr || output == nullptr)
        return FALSE;
    if (input->CallbackType != MemoryCallback)
        return TRUE;
#if defined(TDUR_CRASH_HELPER_STALL)
    // Test variant only (tdur_dump_helper_stall): stays inside the dump so
    // that a test can stop the helper while the target is being dumped.
    if (collector->next == 0)
        Sleep(TDUR_CRASH_HELPER_STALL);
#endif
    if (collector->next >= collector->ranges.size())
        return FALSE;
    const auto& range = collector->ranges[collector->next++];
    output->MemoryBase = range.base;
    output->MemorySize = range.size;
    return TRUE;
}

struct Details {
    std::uint64_t wow64_cell = 0;
    std::uint32_t wow64_target = 0;
    std::string wow64_region = "unreadable";
    std::uint32_t loader_owner = 0;
    std::uint32_t fast_peb_owner = 0;
    bool owners_known = false;
    std::uint32_t code = 0;
    std::uint32_t address = 0;
    std::uint32_t parameters = 0;
    std::uint32_t info0 = 0;
    std::uint32_t info1 = 0;
    bool exception_read = false;
};

// x86 layouts, identical in the helper and in the target (both 32 bits).
struct CriticalSection32 {
    std::uint32_t debug_info, lock_count, recursion_count, owning_thread,
        lock_semaphore, spin_count;
};
static_assert(sizeof(CriticalSection32) == 24);

void CollectRanges(const Arguments& value, Collector& collector,
                   Details& details) {
    // The game image is the first module of the target.
    std::array<HMODULE, 1> first{};
    DWORD needed = 0;
    MODULEINFO module{};
    if (EnumProcessModulesEx(value.process, first.data(), sizeof(first),
                             &needed, LIST_MODULES_32BIT) &&
        needed >= sizeof(HMODULE) &&
        GetModuleInformation(value.process, first[0], &module,
                             sizeof(module))) {
        collector.image_base =
            reinterpret_cast<std::uintptr_t>(module.lpBaseOfDll);
        collector.image_end = collector.image_base + module.SizeOfImage;
    } else {
        collector.notes.push_back("game-image-unknown");
    }
    if (value.crash) {
        std::array<std::uint32_t, 2> pointers{};
        EXCEPTION_RECORD record{};
        if (collector.read(value.pointers, pointers) &&
            collector.read(pointers[0], record)) {
            details.exception_read = true;
            details.code = record.ExceptionCode;
            details.address = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(record.ExceptionAddress));
            details.parameters = record.NumberParameters;
            if (record.NumberParameters >= 2) {
                details.info0 =
                    static_cast<std::uint32_t>(record.ExceptionInformation[0]);
                details.info1 =
                    static_cast<std::uint32_t>(record.ExceptionInformation[1]);
            }
            // (a) Around the faulting address, private or mapped memory only.
            collector.add(details.address, 2048, 2048, true, "fault-address");
            // (b) Around the accessed address.
            if (record.NumberParameters >= 2)
                collector.add(details.info1, 128, 128, true, "access-target");
        } else {
            collector.notes.push_back("exception-record-unreadable");
        }
    }
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    std::array<HMODULE, 64> modules{};
    bool same_ntdll = false;
    if (EnumProcessModulesEx(value.process, modules.data(), sizeof(modules),
                             &needed, LIST_MODULES_32BIT))
        for (DWORD index = 0;
             index < std::min<DWORD>(needed / sizeof(HMODULE), 64); ++index)
            same_ntdll = same_ntdll || modules[index] == ntdll;
    if (!same_ntdll) {
        collector.notes.push_back("ntdll_base_mismatch");
    } else {
        // (c) ntdll!Wow64Transition and 32 bytes at its target.
        const auto cell = reinterpret_cast<std::uintptr_t>(
            GetProcAddress(ntdll, "Wow64Transition"));
        if (cell != 0 && collector.read(cell, details.wow64_target)) {
            details.wow64_cell = cell;
            collector.add(cell, 0, 4, false, "wow64-transition-cell");
            MEMORY_BASIC_INFORMATION info{};
            if (collector.query(details.wow64_target, info)) {
                details.wow64_region = RegionKind(info);
                if (!collector.in_image(details.wow64_target,
                                        details.wow64_target + 32))
                    collector.add(details.wow64_target, 0, 32, false,
                                  "wow64-transition-target");
            }
        } else {
            collector.notes.push_back("wow64-transition-unreadable");
        }
        // (d) First 16 bytes of ntdll!NtWaitForMultipleObjects.
        const auto wait = reinterpret_cast<std::uintptr_t>(
            GetProcAddress(ntdll, "NtWaitForMultipleObjects"));
        if (wait != 0)
            collector.add(wait, 0, 16, false, "nt-wait-multiple-stub");
    }
    // (e) FastPebLock (PEB32 + 0x1C) and LoaderLock (PEB32 + 0xA0).
    const auto query = std::bit_cast<QueryProcess>(
        GetProcAddress(ntdll, "NtQueryInformationProcess"));
    PROCESS_BASIC_INFORMATION basic{};
    ULONG length = 0;
    if (query != nullptr &&
        query(value.process, ProcessBasicInformation, &basic, sizeof(basic),
              &length) >= 0 &&
        basic.PebBaseAddress != nullptr) {
        const auto peb = reinterpret_cast<std::uintptr_t>(basic.PebBaseAddress);
        std::uint32_t fast = 0, loader = 0;
        CriticalSection32 lock{};
        if (collector.read(peb + 0x1C, fast) && collector.read(fast, lock)) {
            details.fast_peb_owner = lock.owning_thread;
            collector.add(fast, 0, 24, false, "fast-peb-lock");
        }
        if (collector.read(peb + 0xA0, loader) &&
            collector.read(loader, lock)) {
            details.loader_owner = lock.owning_thread;
            details.owners_known = true;
            collector.add(loader, 0, 24, false, "loader-lock");
        }
    } else {
        collector.notes.push_back("peb-unreadable");
    }
}

bool ResetFile(HANDLE file) {
    LARGE_INTEGER start{};
    return SetFilePointerEx(file, start, nullptr, FILE_BEGIN) &&
           SetEndOfFile(file);
}

bool WriteAll(HANDLE file, const std::string& text) {
    DWORD written = 0;
    return WriteFile(file, text.data(), static_cast<DWORD>(text.size()),
                     &written, nullptr) &&
           written == text.size() && FlushFileBuffers(file);
}

int SelfCheck() {
    Dbghelp dbghelp;
    if (!LoadDbghelp(dbghelp)) {
        std::printf("{\"self_check\":false,\"error\":%lu}\n", GetLastError());
        return ExitDbghelp;
    }
    std::printf("{\"self_check\":true,\"dbghelp\":%s,\"version\":%s,"
                "\"symopt_ignore_cvrec\":%s,\"symbols_loaded\":false}\n",
                Json(dbghelp.path).c_str(), Json(dbghelp.version).c_str(),
                (dbghelp.options & SYMOPT_IGNORE_CVREC) != 0 ? "true"
                                                             : "false");
    return ExitDumped;
}

int Dump(const Arguments& value) {
    if (!IdentityMatches(value))
        return ExitIdentity;
    Dbghelp dbghelp;
    if (!LoadDbghelp(dbghelp))
        return ExitDbghelp;
    const auto start = std::chrono::steady_clock::now();
    Collector collector;
    collector.process = value.process;
    Details details;
    CollectRanges(value, collector, details);
    MINIDUMP_CALLBACK_INFORMATION callback{DumpCallback, &collector};
    MINIDUMP_EXCEPTION_INFORMATION exception{
        value.thread,
        reinterpret_cast<PEXCEPTION_POINTERS>(
            static_cast<std::uintptr_t>(value.pointers)),
        TRUE};
    bool stream = value.crash;
    DWORD first_error = 0;
    BOOL written =
        dbghelp.write(value.process, value.pid, value.dump, DumpType,
                      stream ? &exception : nullptr, nullptr, &callback);
    if (!written && stream) {
        // Retry without the exception stream on an emptied file.
        first_error = GetLastError();
        stream = false;
        collector.next = 0;
        written = ResetFile(value.dump) &&
                  dbghelp.write(value.process, value.pid, value.dump, DumpType,
                                nullptr, nullptr, &callback);
    }
    const DWORD error = written ? 0 : GetLastError();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count();
    LARGE_INTEGER bytes{};
    GetFileSizeEx(value.dump, &bytes);

    std::string summary = "{\"kind\":";
    summary += value.crash ? "\"crash\"" : "\"hang\"";
    summary += ",\"pid\":" + std::to_string(value.pid);
    summary += ",\"thread_id\":" + std::to_string(value.thread);
    summary += ",\"flags\":" + Hex(static_cast<std::uint32_t>(DumpType));
    summary += ",\"written\":" + std::string(written ? "true" : "false");
    summary +=
        ",\"exception_stream\":" + std::string(stream ? "true" : "false");
    summary += ",\"first_error\":" + std::to_string(first_error);
    summary += ",\"error\":" + std::to_string(error);
    summary += ",\"bytes\":" + std::to_string(bytes.QuadPart);
    summary += ",\"ms\":" + std::to_string(ms);
    summary += ",\"dbghelp\":" + Json(dbghelp.path);
    summary += ",\"dbghelp_version\":" + Json(dbghelp.version);
    summary +=
        ",\"symopt_ignore_cvrec\":" +
        std::string((dbghelp.options & SYMOPT_IGNORE_CVREC) != 0 ? "true"
                                                                 : "false");
    summary += ",\"symbols_loaded\":false";
    if (value.crash) {
        summary += ",\"exception\":";
        if (details.exception_read)
            summary += "{\"code\":" + Hex(details.code) +
                       ",\"address\":" + Hex(details.address) +
                       ",\"parameters\":" + std::to_string(details.parameters) +
                       ",\"info\":[" + Hex(details.info0) + "," +
                       Hex(details.info1) + "]}";
        else
            summary += "null";
    }
    summary += ",\"wow64_transition\":{\"pointer\":" + Hex(details.wow64_cell) +
               ",\"target\":" + Hex(details.wow64_target) +
               ",\"target_region\":" + Json(details.wow64_region) + "}";
    summary += ",\"loader_lock_owner_tid\":" +
               (details.owners_known ? std::to_string(details.loader_owner)
                                     : std::string("null"));
    summary += ",\"fast_peb_lock_owner_tid\":" +
               (details.owners_known ? std::to_string(details.fast_peb_owner)
                                     : std::string("null"));
    summary += ",\"game_image\":{\"base\":" + Hex(collector.image_base) +
               ",\"end\":" + Hex(collector.image_end) + "}";
    summary += ",\"added_ranges\":[";
    for (std::size_t index = 0; index < collector.ranges.size(); ++index) {
        const auto& range = collector.ranges[index];
        summary += (index ? "," : "") + std::string("{\"base\":") +
                   Hex(range.base) + ",\"size\":" + std::to_string(range.size) +
                   ",\"why\":" + Json(range.why) + "}";
    }
    summary += "],\"notes\":[";
    for (std::size_t index = 0; index < collector.notes.size(); ++index)
        summary += (index ? "," : "") + Json(collector.notes[index]);
    summary += "]}\n";
    if (!WriteAll(value.summary, summary))
        return ExitSummary;
    return written ? ExitDumped : ExitDumpFailed;
}
} // namespace

int wmain(int argc, wchar_t* argv[]) {
    // First instruction: later loads come from System32 only.
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
                 SEM_NOOPENFILEERRORBOX);
    Arguments value;
    if (!Parse(argc, argv, value))
        return ExitArguments;
    return value.self_check ? SelfCheck() : Dump(value);
}
