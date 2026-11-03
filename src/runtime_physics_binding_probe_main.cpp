// A fixed-profile observer. It never writes target memory or calls target code.
#include "runtime_readonly_image.hpp"
#include "tdur/diagnostics/physics_binding.hpp"
#include "tdur/game/installation.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
namespace fs = std::filesystem;
using namespace tdur::diagnostics;

std::uint64_t parse_decimal(std::wstring_view text) {
    if (text.empty())
        throw std::runtime_error(
            "Expected a nonempty unsigned decimal integer");
    std::uint64_t value{};
    for (const wchar_t c : text) {
        if (c < L'0' || c > L'9')
            throw std::runtime_error(
                "Only unsigned decimal integers are accepted");
        const auto digit = static_cast<unsigned>(c - L'0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
            throw std::runtime_error("Unsigned integer is out of range");
        value = value * 10 + digit;
    }
    return value;
}

std::uint64_t creation_time(HANDLE process) noexcept {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process, &creation, &exit, &kernel, &user))
        return 0;
    return (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32) |
           creation.dwLowDateTime;
}

struct IdentityHeaderWord {
    std::uint32_t rva{};
    std::uint32_t value{};
};

struct PE32IdentityHeader {
    std::uint32_t dos_word{};
    std::uint32_t nt_offset{};
    std::uint32_t signature{};
    std::uint32_t machine_sections{};
    std::uint32_t magic_linker{};
    std::uint32_t image_base{};
    std::uint32_t image_size{};
};

// Exact read-only profile measured in official trial22. The runtime header
// describes a smaller extent than Toolhelp's loader entry. Neither arbitrary
// smaller headers nor the secondary executable's five-section header match.
bool official_runtime_header(const PE32IdentityHeader& header,
                             std::uint64_t loader_base,
                             std::uint64_t loader_size) noexcept {
    return loader_base == 0x00400000 && loader_size == 0x01bb0000 &&
           header.dos_word == 0x00905a4d && header.nt_offset == 0x138 &&
           header.signature == 0x4550 &&
           header.machine_sections == 0x0004014c &&
           header.magic_linker == 0x0a07010b &&
           header.image_base == loader_base && header.image_size == 0x012ad000;
}

bool within_image_extent(std::uint64_t address, std::uint64_t size,
                         std::uint64_t base, std::uint64_t extent) noexcept {
    return address >= base && address - base <= extent &&
           size <= extent - (address - base);
}

template <class ReadWord>
bool inspect_identity_header(ReadWord read, std::uint64_t loader_base,
                             std::uint64_t loader_size, bool verified,
                             std::uint64_t& runtime_size) {
    runtime_size = 0;
    if (loader_size < 64 ||
        (verified && (loader_base != 0x400000 || loader_size != 0x01bb0000)))
        return false;
    PE32IdentityHeader header{};
    header.dos_word = read(0);
    if ((header.dos_word & 0xffffU) != IMAGE_DOS_SIGNATURE ||
        (verified && header.dos_word != 0x00905a4d))
        return false;
    const auto nt = header.nt_offset = read(0x3c);
    // Do not follow an unknown production e_lfanew, even within Toolhelp's
    // larger extent. Generic own fixtures cannot attest an official SHA.
    if ((verified && nt != 0x138) || nt < 64 ||
        static_cast<std::uint64_t>(nt) + 24 + 60 > loader_size)
        return false;
    header.signature = read(nt);
    header.machine_sections = read(nt + 4);
    header.magic_linker = read(nt + 24);
    header.image_base = read(nt + 24 + 28);
    runtime_size = header.image_size = read(nt + 24 + 56);
    if (verified)
        return official_runtime_header(header, loader_base, loader_size);
    return header.signature == IMAGE_NT_SIGNATURE &&
           (header.machine_sections & 0xffffU) == IMAGE_FILE_MACHINE_I386 &&
           (header.magic_linker & 0xffffU) == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
           header.image_base == loader_base && header.image_size == loader_size;
}

class WindowsBindingReader final : public PhysicsBindingReader {
  public:
    // This private adapter is constructed with verified=true only after the
    // production CLI has locked and hashed the expected official image.
    // The self-test uses false and cannot pass collector admission.
    WindowsBindingReader(ReadOnlyImage& image,
                         PhysicsBindingExpectedProcess expected, bool verified)
        : image_(image), expected_(expected), verified_(verified) {
        process_.reset(
            OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                        FALSE, expected.pid));
        if (!process_ || GetProcessId(process_.get()) != expected.pid ||
            creation_time(process_.get()) != expected.creation_filetime ||
            image_.creation() != expected.creation_filetime)
            throw std::runtime_error(
                "Process creation does not match the owned launch");
        LARGE_INTEGER frequency{};
        if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
            throw std::runtime_error(
                "Cannot establish the observation QPC frequency");
        frequency_ = static_cast<std::uint64_t>(frequency.QuadPart);
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        maximum_user_address_ =
            std::min<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
                                        system.lpMaximumApplicationAddress),
                                    UINT32_MAX);
        pe32_ = inspect_pe32();
    }

    PhysicsBindingIdentity identity() noexcept override {
        PhysicsBindingIdentity id{};
        if (GetProcessId(process_.get()) != expected_.pid)
            return id;
        id.pid = expected_.pid;
        id.creation_filetime = creation_time(process_.get());
        if (image_.base() <= UINT32_MAX)
            id.image_base = static_cast<std::uint32_t>(image_.base());
        id.image_size = runtime_image_size_;
        id.maximum_user_address = maximum_user_address_;
        id.qpc_frequency = frequency_;
        id.official_sha_verified = verified_;
        id.pe32_i386 = pe32_;
        return id;
    }

    bool timestamp(std::uint64_t& tick) noexcept override {
        LARGE_INTEGER value{};
        if (!QueryPerformanceCounter(&value) || value.QuadPart < 0)
            return false;
        tick = static_cast<std::uint64_t>(value.QuadPart);
        return true;
    }

    bool query(std::uint32_t address,
               PhysicsBindingPage& result) noexcept override {
        MEMORY_BASIC_INFORMATION page{};
        if (VirtualQueryEx(process_.get(),
                           reinterpret_cast<const void*>(
                               static_cast<std::uintptr_t>(address)),
                           &page, sizeof(page)) != sizeof(page))
            return false;
        result = {};
        result.begin = reinterpret_cast<std::uintptr_t>(page.BaseAddress);
        result.size = page.RegionSize;
        result.allocation_base =
            reinterpret_cast<std::uintptr_t>(page.AllocationBase);
        result.type = page.Type == MEM_IMAGE ? PhysicsBindingMemoryType::image
                      : page.Type == MEM_PRIVATE
                          ? PhysicsBindingMemoryType::private_memory
                          : PhysicsBindingMemoryType::other;
        result.committed = page.State == MEM_COMMIT;
        result.guard = (page.Protect & PAGE_GUARD) != 0;
        const auto protection = page.Protect & 0xffU;
        result.readable =
            protection == PAGE_READONLY || protection == PAGE_READWRITE ||
            protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
            protection == PAGE_EXECUTE_READWRITE ||
            protection == PAGE_EXECUTE_WRITECOPY;
        result.writable = protection == PAGE_READWRITE ||
                          protection == PAGE_WRITECOPY ||
                          protection == PAGE_EXECUTE_READWRITE ||
                          protection == PAGE_EXECUTE_WRITECOPY;
        result.executable = protection == PAGE_EXECUTE ||
                            protection == PAGE_EXECUTE_READ ||
                            protection == PAGE_EXECUTE_READWRITE ||
                            protection == PAGE_EXECUTE_WRITECOPY;
        return true;
    }

    PhysicsBindingTransfer
    read(std::uint32_t address,
         std::span<std::byte> destination) noexcept override {
        if (destination.size() != 1 && destination.size() != 4)
            return {};
        // Recheck data pages on the same-creation handle for both image and
        // heap payloads. Direct RPM preserves its partial transfer count even
        // on error; ReadOnlyImage still supplies identity/header validation.
        const auto end =
            static_cast<std::uint64_t>(address) + destination.size();
        if (address == 0 || end > maximum_user_address_ + 1)
            return {};
        const bool image_range = within_image_extent(
            address, destination.size(), image_.base(), runtime_image_size_);
        for (auto cursor = static_cast<std::uint64_t>(address); cursor < end;) {
            PhysicsBindingPage page{};
            if (!query(static_cast<std::uint32_t>(cursor), page) ||
                !page.committed || page.guard || !page.readable ||
                page.executable || page.size == 0 || page.begin > cursor ||
                page.begin + page.size <= cursor)
                return {};
            const bool main_image =
                page.type == PhysicsBindingMemoryType::image &&
                page.allocation_base == image_.base();
            if ((main_image && !image_range) || (image_range && !main_image) ||
                (!image_range && !main_image &&
                 page.type != PhysicsBindingMemoryType::private_memory))
                return {};
            cursor = page.begin + page.size;
        }
        SIZE_T received{};
        const bool success =
            ReadProcessMemory(process_.get(),
                              reinterpret_cast<const void*>(
                                  static_cast<std::uintptr_t>(address)),
                              destination.data(), destination.size(),
                              &received) != FALSE;
        return {success, received};
    }

    std::uint32_t identity_header_bytes() const noexcept {
        return header_bytes_;
    }
    std::uint64_t loader_image_size() const noexcept { return image_.size(); }
    std::span<const IdentityHeaderWord> identity_header_words() const noexcept {
        return std::span(header_words_).first(header_bytes_ / 4);
    }

  private:
    std::uint32_t image_word(std::uint32_t rva) {
        if (header_bytes_ / 4 >= header_words_.size())
            throw std::logic_error("PE identity header read budget exceeded");
        header_bytes_ += 4;
        const auto bytes = image_.scalar(rva, 4);
        std::uint32_t result{};
        std::memcpy(&result, bytes.data(), sizeof(result));
        header_words_[header_bytes_ / 4 - 1] = {rva, result};
        return result;
    }

    bool inspect_pe32() {
        return inspect_identity_header(
            [&](std::uint32_t rva) { return image_word(rva); }, image_.base(),
            image_.size(), verified_, runtime_image_size_);
    }

    ReadOnlyImage& image_;
    PhysicsBindingExpectedProcess expected_;
    Handle process_;
    bool verified_{};
    bool pe32_{};
    std::uint64_t frequency_{};
    std::uint64_t maximum_user_address_{};
    std::uint64_t runtime_image_size_{};
    std::uint32_t header_bytes_{};
    std::array<IdentityHeaderWord, 7> header_words_{};
};

template <class T>
void optional_json(std::ostream& out, const std::optional<T>& value) {
    if (value)
        out << static_cast<std::uint64_t>(*value);
    else
        out << "null";
}

std::string report_json(const PhysicsBindingObservation& observation,
                        PhysicsBindingExpectedProcess expected,
                        std::uint32_t identity_header_bytes,
                        std::span<const IdentityHeaderWord> header_words,
                        std::uint64_t loader_image_size) {
    std::ostringstream out;
    const auto& id = observation.identity;
    out << "{\"pid\":" << expected.pid
        << ",\"expected_creation_filetime\":" << expected.creation_filetime
        << ",\"creation_filetime\":" << id.creation_filetime
        << ",\"source_sha256\":\"" << tdur::game::audited_executable_sha256
        << "\",\"image_base\":" << id.image_base
        << ",\"image_size\":" << id.image_size
        << ",\"loader_image_size\":" << loader_image_size
        << ",\"qpc_frequency\":" << id.qpc_frequency
        << ",\"maximum_user_address\":" << id.maximum_user_address
        << ",\"pe32_header_identity_verified\":"
        << (id.pe32_i386 ? "true" : "false") << ",\"identity_sha_verified\":"
        << (id.official_sha_verified ? "true" : "false")
        << ",\"identity_pe_header_bytes\":" << identity_header_bytes
        << ",\"identity_header_words\":[";
    for (std::size_t i = 0; i < header_words.size(); ++i) {
        if (i)
            out << ',';
        out << "{\"rva\":" << header_words[i].rva
            << ",\"value\":" << header_words[i].value << '}';
    }
    out << "],\"status\":\"" << physics_binding_status_name(observation.status)
        << "\",\"read_only\":true,\"source_executable_verified\":true,"
           "\"source_executable_locked\":true,\"atomic_snapshot\":false,"
           "\"object_lifetime_validated\":false,\"aba_excluded\":false,"
           "\"native_call_authorized\":false,\"maximum_payload_bytes\":66,"
           "\"requested_bytes\":"
        << observation.requested_bytes
        << ",\"received_bytes\":" << observation.received_bytes
        << ",\"passes\":[";
    for (std::size_t index = 0; index < observation.passes.size(); ++index) {
        const auto& pass = observation.passes[index];
        out << (index ? "," : "") << "{\"status\":\""
            << physics_binding_status_name(pass.status) << "\",\"at\":\""
            << physics_binding_field_name(pass.at) << "\",\"begin_qpc\":";
        optional_json(out, pass.begin_qpc);
        out << ",\"end_qpc\":";
        optional_json(out, pass.end_qpc);
        out << ",\"requested_bytes\":" << pass.requested_bytes
            << ",\"received_bytes\":" << pass.received_bytes;
        for (std::size_t field = 0; field < pass.values.size(); ++field) {
            out << ",\""
                << physics_binding_field_name(
                       static_cast<PhysicsBindingField>(field))
                << "\":";
            optional_json(out, pass.values[field]);
        }
        out << ",\"ed\":";
        optional_json(out, pass.ed);
        out << '}';
    }
    out << "]}\n";
    return out.str();
}

void write_new(const fs::path& path, std::string_view text) {
    Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                            nullptr));
    if (file.get() == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Binding report must be a new file");
    DWORD written{};
    if (text.size() > MAXDWORD ||
        !WriteFile(file.get(), text.data(), static_cast<DWORD>(text.size()),
                   &written, nullptr) ||
        written != text.size())
        throw std::runtime_error("Cannot write the complete binding report");
}

fs::path own_image() {
    std::wstring name(32768, L'\0');
    const auto count = GetModuleFileNameW(nullptr, name.data(),
                                          static_cast<DWORD>(name.size()));
    if (!count || count >= name.size())
        throw std::runtime_error("Cannot locate the fixture image");
    name.resize(count);
    return name;
}

std::uint32_t fixture_word = 0x89abcdef;
const std::uint32_t fixture_table = 0x12345678;

int self_test() {
    if constexpr (sizeof(void*) != 4)
        throw std::runtime_error(
            "Build the source-owned page fixture as Win32");
    unsigned checks{};
    const auto require = [&](bool condition, const char* message) {
        ++checks;
        if (!condition)
            throw std::runtime_error(message);
    };
    const PE32IdentityHeader known{0x00905a4d, 0x138,    0x4550,    0x0004014c,
                                   0x0a07010b, 0x400000, 0x012ad000};
    require(official_runtime_header(known, 0x400000, 0x01bb0000),
            "Exact measured runtime/loader tuple is classified");
    for (const auto field :
         {&PE32IdentityHeader::dos_word, &PE32IdentityHeader::nt_offset,
          &PE32IdentityHeader::signature, &PE32IdentityHeader::machine_sections,
          &PE32IdentityHeader::magic_linker, &PE32IdentityHeader::image_base,
          &PE32IdentityHeader::image_size}) {
        for (const auto mask : {1U, 0x10000U, 0x80000000U}) {
            auto changed = known;
            changed.*field ^= mask;
            require(!official_runtime_header(changed, 0x400000, 0x01bb0000),
                    "A changed header word cannot match the exact profile");
        }
    }
    auto secondary = known;
    secondary.machine_sections = 0x0005014c;
    secondary.image_size = 0x012af000;
    require(!official_runtime_header(secondary, 0x400000, 0x01bb0000),
            "Secondary executable header is not the official runtime tuple");
    require(!official_runtime_header(known, 0x410000, 0x01bb0000) &&
                !official_runtime_header(known, 0x400000, 0x012ad000),
            "Runtime profile cannot replace the loader identity");
    require(within_image_extent(0x016acfff, 1, 0x400000, 0x012ad000) &&
                within_image_extent(0x016acffc, 4, 0x400000, 0x012ad000),
            "Last complete runtime payloads remain within the smaller extent");
    for (const auto [start, count] : {std::pair{0x016ad000ULL, 1ULL},
                                      {0x016acfffULL, 4ULL},
                                      {0x003fffffULL, 4ULL},
                                      {UINT64_MAX, 4ULL}})
        require(!within_image_extent(start, count, 0x400000, 0x012ad000),
                "Loader-only, crossing and wrapping payloads are refused");
    const std::array<IdentityHeaderWord, 7> measured{
        {{0, known.dos_word},
         {0x3c, known.nt_offset},
         {0x138, known.signature},
         {0x13c, known.machine_sections},
         {0x150, known.magic_linker},
         {0x16c, known.image_base},
         {0x188, known.image_size}}};
    unsigned measured_reads{};
    std::uint64_t measured_extent{};
    require(
        inspect_identity_header(
            [&](std::uint32_t rva) {
                require(
                    measured_reads < measured.size() &&
                        rva == measured[measured_reads].rva,
                    "Measured profile follows exactly the seven cached RVAs");
                return measured[measured_reads++].value;
            },
            0x400000, 0x01bb0000, true, measured_extent) &&
            measured_reads == 7 && measured_extent == known.image_size,
        "Measured profile retains its runtime bound after 28 header bytes");
    for (const auto dos : {0U, 0x00905a4cU, 0x01905a4dU}) {
        unsigned reads{};
        std::uint64_t extent = UINT64_MAX;
        require(!inspect_identity_header(
                    [&](std::uint32_t rva) {
                        ++reads;
                        require(rva == 0, "An unknown DOS header was followed");
                        return dos;
                    },
                    0x400000, 0x01bb0000, true, extent) &&
                    reads == 1 && extent == 0,
                "Unknown production DOS word stops after four header bytes");
    }
    for (const auto nt : {0U, 64U, 0xd90U, 0x012ad000U, 0xffffffffU}) {
        unsigned reads{};
        std::uint64_t extent = UINT64_MAX;
        const auto accepted = inspect_identity_header(
            [&](std::uint32_t rva) {
                ++reads;
                require(rva == 0 || rva == 0x3c,
                        "Unknown production e_lfanew was followed");
                return rva == 0 ? known.dos_word : nt;
            },
            0x400000, 0x01bb0000, true, extent);
        require(!accepted && reads == 2 && extent == 0,
                "Unknown production NT offset stops after eight header bytes");
    }
    for (const auto loader : {std::pair{0x410000ULL, 0x01bb0000ULL},
                              std::pair{0x400000ULL, 0x012ad000ULL}}) {
        unsigned reads{};
        std::uint64_t extent = UINT64_MAX;
        require(!inspect_identity_header(
                    [&](std::uint32_t) {
                        ++reads;
                        return 0U;
                    },
                    loader.first, loader.second, true, extent) &&
                    reads == 0 && extent == 0,
                "Unexpected production loader identity refuses before reads");
    }
    ReadOnlyImage image(GetCurrentProcessId(), own_image());
    const PhysicsBindingExpectedProcess expected{GetCurrentProcessId(),
                                                 image.creation()};
    WindowsBindingReader reader(image, expected, false);
    require(reader.identity().pe32_i386 &&
                !reader.identity().official_sha_verified &&
                reader.identity().image_size == image.size() &&
                reader.loader_image_size() == image.size(),
            "Own fixture retains matching extents without official admission");
    const auto words = reader.identity_header_words();
    require(!words.empty() && words.size() <= 7 &&
                words.size() * 4 == reader.identity_header_bytes(),
            "Cached PE words retain the bounded successful read count");
    for (const auto& cached : words) {
        std::uint32_t own_word{};
        std::memcpy(&own_word,
                    reinterpret_cast<const void*>(image.base() + cached.rva),
                    4);
        require(own_word == cached.value,
                "Identity diagnostics preserve the already-read source-owned "
                "header word");
    }
    const auto address = [](const void* value) {
        const auto integer = reinterpret_cast<std::uintptr_t>(value);
        if (integer > UINT32_MAX)
            throw std::runtime_error("Fixture address is not a pointer32");
        return static_cast<std::uint32_t>(integer);
    };
    PhysicsBindingPage page{};
    const auto word = address(&fixture_word);
    require(reader.query(word, page) && page.committed && page.readable &&
                page.writable && !page.executable &&
                page.type == PhysicsBindingMemoryType::image &&
                page.allocation_base == image.base(),
            "Fixture image-data page");
    require(reader.query(address(&fixture_table), page) && page.readable &&
                !page.writable && !page.executable,
            "Fixture immutable table page");
    std::array<std::byte, 4> bytes{};
    auto transfer = reader.read(word, bytes);
    std::uint32_t copied{};
    std::memcpy(&copied, bytes.data(), 4);
    require(transfer.success && transfer.received == 4 &&
                copied == fixture_word,
            "Real main-image pointer32 read");
    auto* memory = static_cast<std::uint32_t*>(
        VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!memory)
        throw std::runtime_error("Cannot allocate source-owned fixture pages");
    struct Free {
        void* pointer;
        ~Free() { VirtualFree(pointer, 0, MEM_RELEASE); }
    } free{memory};
    memory[0] = address(memory + 2);
    memory[2] = 0x31415926;
    require(reader.query(address(memory), page) && page.committed &&
                page.readable && page.writable && !page.executable &&
                page.type == PhysicsBindingMemoryType::private_memory,
            "Real private-object page");
    transfer = reader.read(address(memory), bytes);
    std::memcpy(&copied, bytes.data(), 4);
    require(transfer.success && transfer.received == 4 &&
                copied == address(memory + 2),
            "Real heap pointer32 is preserved");
    transfer = reader.read(copied, bytes);
    std::memcpy(&copied, bytes.data(), 4);
    require(transfer.success && transfer.received == 4 && copied == 0x31415926,
            "Real heap pointee is read without native calls");
    DWORD old{};
    require(VirtualProtect(memory + 1024, 4096, PAGE_NOACCESS, &old) != FALSE,
            "Install source-owned noaccess page");
    require(reader.query(address(memory + 1024), page) && !page.readable,
            "Noaccess permission observed");
    require(!reader.read(address(memory + 1024), bytes).success,
            "Noaccess payload refused");
    std::array<std::byte, 4> crossing{};
    require(!reader.read(address(memory) + 4094, crossing).success,
            "Cross-page payload refused");
    require(VirtualProtect(memory + 1024, 4096, PAGE_READWRITE | PAGE_GUARD,
                           &old) != FALSE,
            "Install source-owned guard page");
    require(reader.query(address(memory + 1024), page) && page.guard,
            "Guard permission observed");
    require(!reader.read(address(memory + 1024), bytes).success,
            "Guard payload refused before touching it");
    require(reader.query(address(memory + 1024), page) && page.guard,
            "Read refusal preserves the guard");
    require(VirtualProtect(memory + 1024, 4096, PAGE_EXECUTE_READ, &old) !=
                FALSE,
            "Install source-owned executable page");
    require(reader.query(address(memory + 1024), page) && page.executable,
            "Executable permission observed");
    require(!reader.read(address(memory + 1024), bytes).success,
            "Executable page rejected as object data");
    const auto observation = collect_physics_binding(reader, expected);
    require(observation.status == PhysicsBindingStatus::identity_refused &&
                observation.requested_bytes == 0,
            "Source-owned image cannot claim official admission");
    require(fixture_word == 0x89abcdef && memory[2] == 0x31415926,
            "Read-only canaries unchanged");
    bool rejected{};
    try {
        WindowsBindingReader stale(
            image, {expected.pid, expected.creation_filetime + 1}, false);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Stale creation FILETIME refused before a binding read");
    require(parse_decimal(L"134378417126074006") == 134378417126074006ULL,
            "Creation FILETIME lost integer precision");
    require(parse_decimal(L"18446744073709551615") == UINT64_MAX,
            "Maximum uint64 decimal was not parsed exactly");
    for (const auto text :
         {L"", L"-1", L"+1", L"1.0", L" 1", L"18446744073709551616"}) {
        bool refused{};
        try {
            static_cast<void>(parse_decimal(text));
        } catch (const std::exception&) {
            refused = true;
        }
        require(refused, "Malformed or overflowing identity integer admitted");
    }
    std::cout << "PASS: " << checks
              << " source-owned Win32 pointer/page/identity checks\n";
    return 0;
}

int run(int argc, wchar_t* argv[]) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--self-test")
        return self_test();
    if (argc != 9 || std::wstring_view(argv[1]) != L"--pid" ||
        std::wstring_view(argv[3]) != L"--expected-creation-filetime" ||
        std::wstring_view(argv[5]) != L"--expected-exe" ||
        std::wstring_view(argv[7]) != L"--output")
        throw std::runtime_error(
            "Usage: tdur_probe_physics_binding --pid N "
            "--expected-creation-filetime U64 --expected-exe PRIVATE_EXE "
            "--output NEW_JSON");
    const auto pid = parse_decimal(argv[2]);
    const auto creation = parse_decimal(argv[4]);
    if (pid == 0 || pid > MAXDWORD || creation == 0)
        throw std::runtime_error("Invalid owned process identity");
    const auto expected_exe = fs::canonical(argv[6]);
    Handle executable_lock(CreateFileW(expected_exe.c_str(), GENERIC_READ,
                                       FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL, nullptr));
    if (executable_lock.get() == INVALID_HANDLE_VALUE)
        throw std::runtime_error(
            "Cannot lock the expected executable read-only");
    const auto inspection =
        tdur::game::inspect_installation(expected_exe.parent_path());
    if (!inspection || !inspection->matches_audited_executable ||
        _wcsicmp(expected_exe.filename().c_str(), L"TestDriveUnlimited.exe") !=
            0)
        throw std::runtime_error(
            "Only the audited official executable is supported");
    const PhysicsBindingExpectedProcess expected{
        static_cast<std::uint32_t>(pid), creation};
    ReadOnlyImage image(expected.pid, expected_exe);
    WindowsBindingReader reader(image, expected, true);
    const auto observation = collect_physics_binding(reader, expected);
    write_new(argv[8],
              report_json(observation, expected, reader.identity_header_bytes(),
                          reader.identity_header_words(),
                          reader.loader_image_size()));
    std::cout << "Physics binding observed: "
              << physics_binding_status_name(observation.status) << ", "
              << observation.requested_bytes
              << "/66 payload bytes; "
                 "no lifetime, ABA or native-call permission claim\n";
    return 0;
}
} // namespace

int wmain(int argc, wchar_t* argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "Physics binding observation refused: " << error.what()
                  << '\n';
        return 1;
    }
}
