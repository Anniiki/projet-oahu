// Read-only observer of the D mode fields, for one identified process of the
// verified official executable. It never writes target memory, changes page
// protection, suspends, hooks, injects input or reads window text. It does
// not reuse or widen ReadOnlyImage: its own reader is limited to the fixed
// catalogue of mode_probe_core.hpp.
// clang-format off
#include <windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>
// clang-format on

#include "runtime_mode_probe_core.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
namespace fs = std::filesystem;
namespace probe = tdur::diagnostics::mode_probe;

// Same identity as tdur/game/installation.hpp (audited executable).
constexpr char official_sha256[] =
    "3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30";
constexpr std::uintmax_t official_size = 16797696;

struct CloseHandleDeleter {
    void operator()(void* handle) const noexcept {
        if (handle && handle != INVALID_HANDLE_VALUE)
            CloseHandle(handle);
    }
};
using Handle = std::unique_ptr<void, CloseHandleDeleter>;

struct ImageFacts {
    std::uint32_t image_base = 0;
    std::uint32_t size_of_image = 0;
};

std::string sha256_hex(const std::vector<unsigned char>& bytes) {
    std::array<unsigned char, 32> digest{};
    if (bytes.size() > std::numeric_limits<ULONG>::max() ||
        !BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                                   const_cast<unsigned char*>(bytes.data()),
                                   static_cast<ULONG>(bytes.size()),
                                   digest.data(),
                                   static_cast<ULONG>(digest.size()))))
        throw std::runtime_error("Cannot hash the executable");
    constexpr char hex[] = "0123456789abcdef";
    std::string text;
    for (const auto byte : digest) {
        text += hex[byte >> 4];
        text += hex[byte & 0xF];
    }
    return text;
}

std::uint32_t file_le32(const std::vector<unsigned char>& bytes,
                        std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4)
        throw std::runtime_error("Executable header is truncated");
    return static_cast<std::uint32_t>(bytes[at]) |
           static_cast<std::uint32_t>(bytes[at + 1]) << 8 |
           static_cast<std::uint32_t>(bytes[at + 2]) << 16 |
           static_cast<std::uint32_t>(bytes[at + 3]) << 24;
}

// Hashes the file on disk and takes ImageBase and SizeOfImage from the same
// bytes. Nothing here reads the target process.
ImageFacts verify_executable(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file || fs::file_size(path) != official_size)
        throw std::runtime_error("Executable is not the official size");
    std::vector<unsigned char> bytes(official_size);
    if (!file.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size())) ||
        file.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("Cannot read the whole executable");
    if (sha256_hex(bytes) != official_sha256)
        throw std::runtime_error("Executable SHA256 is not the official one");
    const auto pe = file_le32(bytes, 0x3C);
    if (file_le32(bytes, pe) != 0x00004550 ||
        (file_le32(bytes, pe + 4) & 0xFFFF) != 0x014C ||
        (file_le32(bytes, pe + 24) & 0xFFFF) != 0x010B)
        throw std::runtime_error("Executable is not a 32-bit PE image");
    return {file_le32(bytes, pe + 24 + 28), file_le32(bytes, pe + 24 + 56)};
}

std::uint64_t filetime(const FILETIME& value) {
    return static_cast<std::uint64_t>(value.dwHighDateTime) << 32 |
           value.dwLowDateTime;
}

bool readable_data(DWORD protect) {
    const auto base = protect & 0xFFU;
    return (base == PAGE_READONLY || base == PAGE_READWRITE ||
            base == PAGE_WRITECOPY) &&
           (protect & (PAGE_GUARD | PAGE_NOCACHE | PAGE_WRITECOMBINE)) == 0;
}

// Reads at most 9 bytes, only from committed, readable, non-executable,
// non-guard pages; image reads must stay inside the verified main image.
class ProcessReader final : public probe::Reader {
  public:
    ProcessReader(HANDLE process, std::uint32_t base, std::uint32_t size)
        : process_(process), base_(base), size_(size) {}

    bool read(probe::Space space, std::uint32_t address,
              std::span<std::byte> out) override {
        if (out.empty() || out.size() > probe::field_length)
            return false;
        const std::uint64_t end = std::uint64_t{address} + out.size();
        if (end > std::uint64_t{std::numeric_limits<std::uint32_t>::max()} + 1)
            return false;
        if (space == probe::Space::image &&
            (address < base_ || end > std::uint64_t{base_} + size_))
            return false;
        for (std::uint64_t cursor = address; cursor < end;) {
            MEMORY_BASIC_INFORMATION region{};
            if (VirtualQueryEx(process_,
                               reinterpret_cast<const void*>(
                                   static_cast<std::uintptr_t>(cursor)),
                               &region, sizeof(region)) != sizeof(region))
                return false;
            if (region.State != MEM_COMMIT || !readable_data(region.Protect))
                return false;
            if (space == probe::Space::image &&
                (region.Type != MEM_IMAGE ||
                 reinterpret_cast<std::uintptr_t>(region.AllocationBase) !=
                     base_))
                return false;
            const auto start =
                reinterpret_cast<std::uintptr_t>(region.BaseAddress);
            const std::uint64_t next = std::uint64_t{start} + region.RegionSize;
            if (start > cursor || next <= cursor)
                return false;
            cursor = next;
        }
        SIZE_T received = 0;
        return ReadProcessMemory(process_,
                                 reinterpret_cast<const void*>(
                                     static_cast<std::uintptr_t>(address)),
                                 out.data(), out.size(), &received) &&
               received == out.size();
    }

  private:
    HANDLE process_;
    std::uint32_t base_;
    std::uint32_t size_;
};

struct Target {
    Handle process;
    std::uint32_t base = 0;
    std::uint32_t size = 0;
};

Target open_target(const probe::Options& options, const fs::path& canonical,
                   const ImageFacts& facts) {
    Target target;
    target.process.reset(
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE,
                    options.pid));
    if (!target.process)
        throw std::runtime_error("Cannot open target read-only");
    BOOL wow64 = FALSE;
    if (!IsWow64Process(target.process.get(), &wow64) || !wow64)
        throw std::runtime_error("Target is not a 32-bit process");
    std::wstring image(32768, L'\0');
    DWORD length = static_cast<DWORD>(image.size());
    if (!QueryFullProcessImageNameW(target.process.get(), 0, image.data(),
                                    &length))
        throw std::runtime_error("Cannot identify target image");
    image.resize(length);
    if (_wcsicmp(fs::canonical(image).c_str(), canonical.c_str()) != 0)
        throw std::runtime_error("Target is not the expected executable");
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(target.process.get(), &creation, &exit, &kernel,
                         &user) ||
        filetime(creation) != options.creation)
        throw std::runtime_error("Target creation time does not match");
    Handle snapshot(CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, options.pid));
    if (snapshot.get() == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot enumerate the target image");
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::uintptr_t base = 0;
    std::size_t size = 0;
    if (Module32FirstW(snapshot.get(), &entry)) {
        do {
            if (_wcsicmp(entry.szExePath, canonical.c_str()) == 0) {
                base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
                size = entry.modBaseSize;
                break;
            }
        } while (Module32NextW(snapshot.get(), &entry));
    }
    if (!base || base > std::numeric_limits<std::uint32_t>::max() ||
        size != facts.size_of_image)
        throw std::runtime_error("Expected main image is not mapped");
    target.base = static_cast<std::uint32_t>(base);
    target.size = static_cast<std::uint32_t>(size);
    return target;
}

bool still_same(const Target& target, std::uint64_t creation) {
    DWORD code = 0;
    FILETIME created{}, exit{}, kernel{}, user{};
    return GetExitCodeProcess(target.process.get(), &code) &&
           code == STILL_ACTIVE &&
           GetProcessTimes(target.process.get(), &created, &exit, &kernel,
                           &user) &&
           filetime(created) == creation;
}

void write_all(HANDLE file, std::string_view text) {
    DWORD written = 0;
    if (text.size() > MAXDWORD ||
        !WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written,
                   nullptr) ||
        written != text.size())
        throw std::runtime_error("Cannot write the complete report");
}

int run(const probe::Options& options) {
    const auto canonical = fs::canonical(options.executable);
    const auto facts = verify_executable(canonical);
    if (facts.image_base != options.preferred)
        throw std::runtime_error("Executable preferred base is not 0x400000");
    const auto target = open_target(options, canonical, facts);
    const auto layout =
        probe::make_layout(target.base, target.size, options.preferred);
    if (!layout)
        throw std::runtime_error(layout.error());
    Handle output(CreateFileW(options.output.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (output.get() == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Output must be a new file");

    ProcessReader reader(target.process.get(), target.base, target.size);
    probe::Journal journal;
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    const auto deadline = start + std::chrono::seconds(options.duration_s);
    const auto interval = std::chrono::milliseconds(options.interval_ms);
    auto next = start;
    bool exited = false;
    while (clock::now() < deadline) {
        if (!still_same(target, options.creation)) {
            exited = true;
            break;
        }
        const auto sample = probe::take_sample(reader, *layout);
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() -
                                                                  start);
        journal.record(static_cast<std::uint64_t>(elapsed.count()), sample);
        next += interval;
        const auto earliest =
            clock::now() + std::chrono::milliseconds(probe::min_interval_ms);
        if (next < earliest)
            next = earliest;
        std::this_thread::sleep_until(std::min(next, deadline));
    }

    std::string text =
        "{\"tool\":\"tdur_probe_mode\",\"pid\":" + std::to_string(options.pid) +
        ",\"executable_sha256\":\"" + official_sha256 + "\",\"relocated\":" +
        (target.base == options.preferred ? "false" : "true") +
        ",\"interval_ms\":" + std::to_string(options.interval_ms) +
        ",\"duration_s\":" + std::to_string(options.duration_s) +
        ",\"target_exited\":" + (exited ? "true" : "false") +
        ",\"consistency\":\"stability by double read, not an "
        "atomic snapshot\",\"hypothesis\":\"G as the "
        "dispatcher this is an ABI hypothesis; no observation "
        "here proves what PC 0x97CE80 reads\",\"changes\":[";
    bool first = true;
    for (const auto& change : journal.changes()) {
        text += (first ? "\n" : ",\n") + probe::render_change(change);
        first = false;
    }
    text += "],\n\"counters\":" + probe::render_counters(journal.counters()) +
            "}\n";
    write_all(output.get(), text);
    std::cout << probe::render_counters(journal.counters()) << '\n';
    return 0;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        std::vector<std::wstring_view> args;
        for (int i = 1; i < argc; ++i)
            args.emplace_back(argv[i]);
        const auto options = probe::parse_options(args);
        if (!options) {
            std::cerr << "usage error: " << options.error() << '\n'
                      << "tdur_probe_mode --pid N --creation-filetime N "
                         "--exe PATH --preferred-base 0x400000 --out NEW "
                         "--duration-s 1..120 [--interval-ms 25..1000]\n";
            return 2;
        }
        return run(*options);
    } catch (const std::exception& error) {
        std::cerr << "refused: " << error.what() << '\n';
        return 1;
    }
}
