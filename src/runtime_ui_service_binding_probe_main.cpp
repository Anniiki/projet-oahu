// Fixed read-only UI metadata observation. Never invokes target methods.
#include "tdur/game/installation.hpp"
#include "ui_service_windows_reader.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

namespace {
namespace fs = std::filesystem;
using namespace tdur::diagnostics;
using namespace tdur::diagnostics::ui_detail;

void write_new(const fs::path& path, const std::string& text) {
    Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                            nullptr));
    if (file.get() == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot create new UI binding evidence");
    DWORD written{};
    if (text.size() > MAXDWORD ||
        !WriteFile(file.get(), text.data(), static_cast<DWORD>(text.size()),
                   &written, nullptr) ||
        written != text.size())
        throw std::runtime_error("Cannot write complete UI binding evidence");
}
template <class T>
void optional_json(std::ostream& out, const std::optional<T>& value) {
    if (value)
        out << *value;
    else
        out << "null";
}
void chain_json(std::ostream& out, const UiServiceBindingChain& chain) {
    out << "{\"status\":\"" << ui_service_binding_status_name(chain.status)
        << "\",\"at\":\"" << ui_service_binding_field_name(chain.at) << "\"}";
}
std::string report_json(const UiServiceBindingObservation& result,
                        UiServiceExpectedProcess expected,
                        const UiWindowsBindingReader& reader) {
    std::ostringstream out;
    out << "{\"schema\":\"tdur-ui-service-binding/v1\",\"pid\":" << expected.pid
        << ",\"creation_filetime\":" << expected.creation_filetime
        << ",\"status\":\"" << ui_service_binding_status_name(result.status)
        << "\",\"image_base\":" << result.identity.image_base
        << ",\"runtime_image_size\":" << result.identity.image_size
        << ",\"loader_image_size\":" << reader.loader_image_size()
        << ",\"maximum_user_address\":" << result.identity.maximum_user_address
        << ",\"qpc_frequency\":" << result.identity.qpc_frequency
        << ",\"official_sha_verified\":"
        << (result.identity.official_sha_verified ? "true" : "false")
        << ",\"pe32_i386\":" << (result.identity.pe32_i386 ? "true" : "false")
        << ",\"metadata_max_requested_bytes\":96,\"metadata_requested_bytes\":"
        << result.requested_bytes
        << ",\"metadata_received_bytes\":" << result.received_bytes
        << ",\"adapter_metadata_attempted_bytes\":"
        << reader.payload_attempted_bytes()
        << ",\"metadata_rpm_requested_bytes\":"
        << reader.payload_rpm_requested_bytes()
        << ",\"metadata_rpm_received_bytes\":"
        << reader.payload_received_bytes()
        << ",\"identity_header_max_requested_bytes\":28,\"identity_header_"
           "requested_bytes\":"
        << reader.identity_header_bytes()
        << ",\"maximum_direct_rpm_requested_bytes\":124,\"direct_rpm_requested_"
           "bytes\":"
        << reader.identity_header_bytes() + reader.payload_rpm_requested_bytes()
        << ",\"atomic_snapshot\":false,\"native_methods_invoked\":false,\"code_"
           "bytes_read\":0,"
           "\"record_or_string_payload_read\":false,\"native_registry_locale_"
           "mapping_established\":false,"
           "\"lifetime_aba_or_thread_permission_established\":false,\"identity_"
           "header_words\":[";
    bool first = true;
    for (const auto& word : reader.identity_header_words()) {
        out << (first ? "" : ",") << "{\"rva\":" << word.rva
            << ",\"value\":" << word.value << '}';
        first = false;
    }
    out << "],\"passes\":[";
    for (std::size_t index = 0; index < result.passes.size(); ++index) {
        const auto& pass = result.passes[index];
        out << (index ? "," : "") << "{\"status\":\""
            << ui_service_binding_status_name(pass.status) << "\",\"at\":\""
            << ui_service_binding_field_name(pass.at)
            << "\",\"requested_bytes\":" << pass.requested_bytes
            << ",\"received_bytes\":" << pass.received_bytes
            << ",\"begin_qpc\":";
        optional_json(out, pass.begin_qpc);
        out << ",\"end_qpc\":";
        optional_json(out, pass.end_qpc);
        out << ",\"host\":";
        chain_json(out, pass.host);
        out << ",\"catalog_root\":";
        chain_json(out, pass.catalog_root);
        out << ",\"descriptors\":[";
        for (std::size_t d = 0; d < pass.descriptors.size(); ++d) {
            out << (d ? "," : "") << "{\"opaque_selector\":" << (d ? 7 : 0)
                << ",\"binding\":";
            chain_json(out, pass.descriptors[d]);
            out << '}';
        }
        out << "],\"values\":{";
        for (std::size_t field = 0; field < pass.values.size(); ++field) {
            out << (field ? "," : "") << '"'
                << ui_service_binding_field_name(
                       static_cast<UiServiceBindingField>(field))
                << "\":";
            optional_json(out, pass.values[field]);
        }
        out << "}}";
    }
    out << "]}\n";
    return out.str();
}

fs::path own_image() {
    std::wstring path(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, path.data(),
                                         static_cast<DWORD>(path.size()));
    if (size == 0 || size >= path.size())
        throw std::runtime_error("Cannot identify own fixture image");
    path.resize(size);
    return fs::canonical(path);
}
int self_test() {
    unsigned checks{};
    const auto require = [&](bool condition, const char* message) {
        ++checks;
        if (!condition)
            throw std::runtime_error(message);
    };
    const PE32IdentityHeader known{0x00905a4d, 0x138,    0x4550,    0x0004014c,
                                   0x0a07010b, 0x400000, 0x012ad000};
    require(official_runtime_header(known, 0x400000, 0x01bb0000),
            "Measured PE profile");
    for (const auto field :
         {&PE32IdentityHeader::dos_word, &PE32IdentityHeader::nt_offset,
          &PE32IdentityHeader::signature, &PE32IdentityHeader::machine_sections,
          &PE32IdentityHeader::magic_linker, &PE32IdentityHeader::image_base,
          &PE32IdentityHeader::image_size}) {
        auto bad = known;
        bad.*field ^= 1U;
        require(!official_runtime_header(bad, 0x400000, 0x01bb0000),
                "Changed measured header refused");
    }
    const std::array<IdentityHeaderWord, 7> measured{
        {{0, known.dos_word},
         {0x3c, known.nt_offset},
         {0x138, known.signature},
         {0x13c, known.machine_sections},
         {0x150, known.magic_linker},
         {0x16c, known.image_base},
         {0x188, known.image_size}}};
    unsigned reads{};
    std::uint64_t extent{};
    require(inspect_identity_header(
                [&](std::uint32_t rva) {
                    require(reads < measured.size() &&
                                measured[reads].rva == rva,
                            "Fixed measured PE read sequence");
                    return measured[reads++].value;
                },
                0x400000, 0x01bb0000, true, extent) &&
                reads == 7 && extent == 0x012ad000,
            "Seven measured DWORDs, no loader-wide dump");
    reads = 0;
    require(!inspect_identity_header(
                [&](std::uint32_t rva) {
                    ++reads;
                    require(rva == 0, "Unknown DOS never followed");
                    return 0U;
                },
                0x400000, 0x01bb0000, true, extent) &&
                reads == 1,
            "Unknown header stops before NT dereference");
    require(within_image_extent(0x16acffc, 4, 0x400000, 0x12ad000) &&
                !within_image_extent(0x16acfff, 4, 0x400000, 0x12ad000),
            "Runtime extent beats larger loader extent");
    ReadOnlyImage image(GetCurrentProcessId(), own_image());
    const UiServiceExpectedProcess expected{GetCurrentProcessId(),
                                            image.creation()};
    UiWindowsBindingReader reader(image, expected, false);
    require(!reader.identity().official_sha_verified &&
                reader.identity_header_bytes() <= 28,
            "Own x64/Win32 fixture cannot attest official SHA");
    void* memory{};
    for (const auto address : {0x20000000U, 0x21000000U}) {
        memory = VirtualAlloc(
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(address)), 8192,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (memory)
            break;
    }
    if (!memory)
        throw std::runtime_error(
            "Cannot allocate own bounded low-address fixture");
    struct Free {
        void* pointer;
        ~Free() { VirtualFree(pointer, 0, MEM_RELEASE); }
    } free{memory};
    auto* words = static_cast<std::uint32_t*>(memory);
    words[0] = 0x31415926;
    const auto address =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(memory));
    UiServiceBindingPage page{};
    require(reader.query(address, page) &&
                page.type == UiServiceMemoryType::private_memory &&
                page.readable && !page.executable,
            "Real own private page query");
    std::array<std::byte, 4> bytes{};
    std::uint32_t copied{};
    auto transfer = reader.read(address, bytes);
    std::memcpy(&copied, bytes.data(), 4);
    require(transfer.success && transfer.received == 4 && copied == words[0],
            "Read actual own DWORD");
    DWORD old{};
    auto* second = static_cast<std::byte*>(memory) + 4096;
    require(VirtualProtect(second, 4096, PAGE_NOACCESS, &old) != FALSE,
            "Own noaccess fixture");
    require(!reader.read(address + 4094, bytes).success,
            "Cross-page noaccess refused before RPM");
    require(VirtualProtect(second, 4096, PAGE_READWRITE | PAGE_GUARD, &old) !=
                FALSE,
            "Own guard fixture");
    require(!reader.read(address + 4096, bytes).success &&
                reader.query(address + 4096, page) && page.guard,
            "Guard refused without clearing it");
    require(VirtualProtect(second, 4096, PAGE_EXECUTE_READ, &old) != FALSE,
            "Own executable fixture");
    require(!reader.read(address + 4096, bytes).success,
            "Executable data read refused");
    while (reader.payload_attempted_bytes() < 96)
        require(reader.read(address, bytes).success,
                "Own reads fit remaining fixed budget");
    require(!reader.read(address, bytes).success &&
                reader.payload_attempted_bytes() == 96 &&
                reader.payload_rpm_requested_bytes() <= 96,
            "Adapter independently enforces metadata budget");
    const auto observation = collect_ui_service_binding(reader, expected);
    require(observation.status == UiServiceBindingStatus::identity_refused &&
                observation.requested_bytes == 0,
            "Own fixture never enters official collector");
    bool rejected{};
    try {
        UiWindowsBindingReader stale(
            image, {expected.pid, expected.creation_filetime + 1}, false);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Stale creation identity refused");
    require(words[0] == 0x31415926, "Source-owned canary preserved");
    require(parse_decimal(L"18446744073709551615") == UINT64_MAX,
            "U64 identity exact");
    for (const auto text :
         {L"", L"-1", L"+1", L"1.0", L" 1", L"18446744073709551616"}) {
        rejected = false;
        try {
            static_cast<void>(parse_decimal(text));
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected, "Malformed identity integer refused");
    }
    const auto json = report_json(observation, expected, reader);
    require(json.find("\"maximum_direct_rpm_requested_bytes\":124") !=
                std::string::npos,
            "Report separates header and metadata caps");
    std::cout << "PASS: " << checks
              << " source-owned UI reader checks; no game opened\n";
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
            "Usage: tdur_probe_ui_services --pid N "
            "--expected-creation-filetime U64 --expected-exe PRIVATE_EXE "
            "--output NEW_JSON");
    const auto pid = parse_decimal(argv[2]), creation = parse_decimal(argv[4]);
    if (pid == 0 || pid > MAXDWORD || creation == 0)
        throw std::runtime_error("Invalid owned process identity");
    const auto executable = fs::canonical(argv[6]);
    Handle lock(CreateFileW(executable.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                            nullptr));
    if (lock.get() == INVALID_HANDLE_VALUE)
        throw std::runtime_error(
            "Cannot deny writes/deletes on expected image");
    const auto installation =
        tdur::game::inspect_installation(executable.parent_path());
    if (!installation || !installation->matches_audited_executable ||
        _wcsicmp(executable.filename().c_str(), L"TestDriveUnlimited.exe") != 0)
        throw std::runtime_error("Only audited official MC1.66A is supported");
    const UiServiceExpectedProcess expected{static_cast<std::uint32_t>(pid),
                                            creation};
    ReadOnlyImage image(expected.pid, executable);
    UiWindowsBindingReader reader(image, expected, true);
    const auto observation = collect_ui_service_binding(reader, expected);
    write_new(argv[8], report_json(observation, expected, reader));
    std::cout << "UI services observed: "
              << ui_service_binding_status_name(observation.status) << ", "
              << observation.requested_bytes << "/96 metadata bytes plus "
              << reader.identity_header_bytes()
              << "/28 identity bytes; no native calls\n";
    return 0;
}
} // namespace
int wmain(int argc, wchar_t* argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "UI observation refused: " << error.what() << '\n';
        return 1;
    }
}
