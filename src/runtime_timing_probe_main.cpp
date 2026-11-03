// Observe only fixed, documented timing/graphics scalars and call cells.
// This process never supplies input, calls game code or writes target memory.
#include "runtime_readonly_image.hpp"
#include "tdur/game/installation.hpp"

#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string_view>
#include <thread>

namespace {
namespace fs = std::filesystem;
using tdur::diagnostics::Handle;
using tdur::diagnostics::ReadOnlyImage;

enum class Kind { u8, u32, i32, f32 };
struct Scalar {
    const char* name;
    std::uint32_t va;
    Kind kind;
};
// Sources: timer 982ac0, timer-vtable dispatch 9b4830/9b4900, WM_ACTIVATEAPP
// 433648, and entry/profiling references 81dfee/81ede4. No pointers followed.
constexpr std::array timing_catalogue{
    Scalar{"clock_milliseconds", 0x016a2c18, Kind::u32},
    Scalar{"application_active", 0x016990c3, Kind::u8},
    Scalar{"timer_total_unscaled_seconds", 0x00fa3fac, Kind::f32},
    Scalar{"timer_absolute_milliseconds", 0x00fa3fb0, Kind::f32},
    Scalar{"timer_delta_unscaled_seconds", 0x00fa3fb4, Kind::f32},
    Scalar{"timer_saved_scale", 0x00fa3fb8, Kind::f32},
    Scalar{"timer_scale", 0x00fa3fbc, Kind::f32},
    Scalar{"timer_total_scaled_seconds", 0x00fa3fc0, Kind::f32},
    Scalar{"timer_delta_scaled_seconds", 0x00fa3fc8, Kind::f32},
    Scalar{"timer_current_raw_seconds", 0x00fa3fcc, Kind::f32},
    Scalar{"physics_integrator_entries", 0x016a69f0, Kind::u32},
    Scalar{"physics_dt_multiplier", 0x00f8a2ec, Kind::f32},
    Scalar{"physics_frequency_integer", 0x00f87ffc, Kind::i32},
    Scalar{"physics_thread_mode_integer", 0x010e765c, Kind::i32},
    Scalar{"physics_delta_source_flag", 0x00f8a2e8, Kind::u8},
    Scalar{"physics_configured_period_seconds", 0x0168d6b8, Kind::f32},
    Scalar{"physics_flag_129beeb", 0x0129beeb, Kind::u8},
    Scalar{"physics_flag_129beec", 0x0129beec, Kind::u8},
    Scalar{"physics_flag_129beed", 0x0129beed, Kind::u8},
    Scalar{"physics_callback_milliseconds", 0x010e7a5c, Kind::u32},
};
// Only the graphics fields directly referenced by the original config
// callbacks, GSRender initialization 996850 and option application 74dca0.
// These are not a read of the surrounding player/profile structure.
constexpr std::array graphics_catalogue{
    Scalar{"config_antialias_integer", 0x011403dc, Kind::i32},
    Scalar{"config_hdri_boolean", 0x011403e0, Kind::u8},
    Scalar{"profile_antialias_integer", 0x01161e7c, Kind::i32},
    Scalar{"profile_hdri_boolean", 0x01161e80, Kind::u8},
    Scalar{"profile_lod_integer", 0x01161e84, Kind::i32},
    Scalar{"renderer_antialias_integer", 0x0114b0ac, Kind::i32},
    Scalar{"renderer_hdri_integer", 0x01149b2c, Kind::i32},
    Scalar{"renderer_lod_integer", 0x0114b0b0, Kind::i32},
};
// Separate opt-in; no address stored in a call cell is followed or invoked.
constexpr std::array adapter_catalogue{
    Scalar{"physics_timer_call_cell", 0x009ec4a8, Kind::u32},
};
constexpr std::uint32_t preferred_base = 0x00400000;

fs::path own_image() {
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(),
                                           static_cast<DWORD>(path.size()));
    if (!length || length >= path.size())
        throw std::runtime_error("Cannot identify fixture executable");
    path.resize(length);
    return path;
}

std::string value_json(const ReadOnlyImage& process, const Scalar& item) {
    const auto bytes = process.scalar(item.va, item.kind == Kind::u8 ? 1 : 4);
    if (item.kind == Kind::u8)
        return std::to_string(std::to_integer<unsigned>(bytes.front()));
    std::uint32_t bits = 0;
    std::memcpy(&bits, bytes.data(), sizeof(bits));
    if (item.kind == Kind::u32)
        return std::to_string(bits);
    if (item.kind == Kind::i32)
        return std::to_string(std::bit_cast<std::int32_t>(bits));
    const auto value = std::bit_cast<float>(bits);
    if (!std::isfinite(value))
        return "null";
    std::ostringstream result;
    result << std::setprecision(std::numeric_limits<float>::max_digits10)
           << value;
    return result.str();
}

void write_new(const fs::path& path, std::string_view text) {
    Handle file(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                            nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                            nullptr));
    if (file.get() == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Timing report must be a new file");
    DWORD written = 0;
    if (text.size() > MAXDWORD ||
        !WriteFile(file.get(), text.data(), static_cast<DWORD>(text.size()),
                   &written, nullptr) ||
        written != text.size())
        throw std::runtime_error("Cannot write complete timing report");
}

// Only the source-owned self-test writes its own fixture state.
std::atomic<std::uint32_t> fixture_counter{17};
volatile float fixture_float = 1.25F;
volatile std::uint8_t fixture_byte = 1;
__declspec(noinline) unsigned fixture_function(unsigned value) {
    return value * 7 + fixture_counter.load();
}

std::uint32_t fixture_rva(const ReadOnlyImage& image,
                          const volatile void* ptr) {
    const auto address = reinterpret_cast<std::uintptr_t>(ptr);
    if (address < image.base() || address - image.base() > UINT32_MAX)
        throw std::runtime_error("Fixture is outside its image");
    return static_cast<std::uint32_t>(address - image.base());
}

int self_test() {
    ReadOnlyImage image(GetCurrentProcessId(), own_image());
    const auto counter = fixture_rva(image, &fixture_counter);
    const auto number = fixture_rva(image, &fixture_float);
    const auto flag = fixture_rva(image, &fixture_byte);
    const auto code =
        fixture_rva(image, std::bit_cast<const void*>(&fixture_function));
    if (value_json(image, {"float", number, Kind::f32}) != "1.25" ||
        value_json(image, {"flag", flag, Kind::u8}) != "1" ||
        value_json(image, {"counter", counter, Kind::u32}) != "17")
        throw std::runtime_error("Typed scalar fixture read failed");
    const auto before = image.code(code, 16);
    std::jthread producer([] {
        for (unsigned index = 0; index < 25; ++index) {
            fixture_counter.fetch_add(1);
            Sleep(2);
        }
    });
    unsigned changing = 0;
    auto prior = value_json(image, {"counter", counter, Kind::u32});
    for (unsigned index = 0; index < 20; ++index) {
        Sleep(4);
        auto now = value_json(image, {"counter", counter, Kind::u32});
        changing += now != prior ? 1U : 0U;
        prior = std::move(now);
    }
    producer.join();
    if (!changing || fixture_counter != 42 || fixture_float != 1.25F ||
        fixture_byte != 1 || before != image.code(code, 16))
        throw std::runtime_error("Progress or read-only canaries failed");
    unsigned refused = 0;
    const auto reject = [&](auto operation) {
        try {
            operation();
        } catch (const std::exception&) {
            ++refused;
        }
    };
    reject([&] { image.scalar(code, 4); });
    reject([&] { image.scalar(counter, 2); });
    reject([&] { image.scalar(static_cast<std::uint32_t>(image.size()), 4); });
    reject([&] { image.code(counter, 4); });
    reject([&] { ReadOnlyImage wrong(0, own_image()); });
    if (refused != 5)
        throw std::runtime_error("Scalar/page/image restrictions failed");
    fixture_float = std::numeric_limits<float>::infinity();
    const auto nonfinite = value_json(image, {"infinity", number, Kind::f32});
    fixture_float = 1.25F;
    if (nonfinite != "null")
        throw std::runtime_error("Non-finite value would produce invalid JSON");
    std::cout << "PASS timing scalar types, live progress, unchanged canaries, "
                 "five refusals and finite JSON\n";
    return 0;
}

int run(int argc, wchar_t* argv[]) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--self-test")
        return self_test();
    const bool extended =
        argc == 9 && std::wstring_view(argv[7]) == L"--profile";
    if ((argc != 7 && !extended) || std::wstring_view(argv[1]) != L"--pid" ||
        std::wstring_view(argv[3]) != L"--expected-exe" ||
        std::wstring_view(argv[5]) != L"--output")
        throw std::runtime_error("Usage: tdur_probe_timing --pid N "
                                 "--expected-exe PRIVATE_EXE --output NEW_JSON "
                                 "[--profile timing|graphics|native-adapter]");
    const std::wstring_view profile = extended ? argv[8] : L"timing";
    if (profile != L"timing" && profile != L"graphics" &&
        profile != L"native-adapter")
        throw std::runtime_error(
            "Scalar profile must be timing, graphics or native-adapter");
    std::vector<Scalar> catalogue(timing_catalogue.begin(),
                                  timing_catalogue.end());
    if (profile == L"graphics" || profile == L"native-adapter")
        catalogue.insert(catalogue.end(), graphics_catalogue.begin(),
                         graphics_catalogue.end());
    if (profile == L"native-adapter")
        catalogue.insert(catalogue.end(), adapter_catalogue.begin(),
                         adapter_catalogue.end());
    unsigned bytes_per_sample = 0;
    for (const auto& item : catalogue)
        bytes_per_sample += item.kind == Kind::u8 ? 1U : 4U;
    std::size_t used = 0;
    const std::wstring pid_text(argv[2]);
    const auto parsed = std::stoull(pid_text, &used);
    if (used != pid_text.size() || !parsed || parsed > MAXDWORD)
        throw std::runtime_error("Invalid PID");
    const auto expected = fs::canonical(argv[4]);
    const auto installation =
        tdur::game::inspect_installation(expected.parent_path());
    if (!installation || !installation->matches_audited_executable ||
        _wcsicmp(expected.filename().c_str(), L"TestDriveUnlimited.exe") != 0)
        throw std::runtime_error(
            "Only the audited official image is supported");
    ReadOnlyImage image(static_cast<DWORD>(parsed), expected);
    LARGE_INTEGER frequency{}, started{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0 ||
        !QueryPerformanceCounter(&started))
        throw std::runtime_error("Probe monotonic clock unavailable");
    const auto micros = [&] {
        LARGE_INTEGER current{};
        if (!QueryPerformanceCounter(&current) ||
            current.QuadPart < started.QuadPart)
            throw std::runtime_error("Probe monotonic clock failed");
        return static_cast<std::uint64_t>(
            (current.QuadPart - started.QuadPart) * 1000000 /
            frequency.QuadPart);
    };
    std::ostringstream report;
    report << "{\"pid\":" << parsed << ",\"image_base\":" << image.base()
           << ",\"creation_filetime\":" << image.creation()
           << ",\"source_sha256\":\"" << tdur::game::audited_executable_sha256
           << "\",\"read_only\":true,\"atomic_snapshot\":false,"
              "\"profile\":\""
           << (profile == L"native-adapter" ? "native-adapter"
               : profile == L"graphics"     ? "graphics"
                                            : "timing")
           << "\",\"bytes_per_sample\":" << bytes_per_sample
           << ",\"catalogue\":[";
    for (std::size_t i = 0; i < catalogue.size(); ++i)
        report << (i ? "," : "") << "{\"name\":\"" << catalogue[i].name
               << "\",\"va\":" << catalogue[i].va
               << ",\"bytes\":" << (catalogue[i].kind == Kind::u8 ? 1 : 4)
               << '}';
    report << "],\"samples\":[";
    std::size_t failures = 0;
    for (unsigned sample = 0; sample < 26; ++sample) {
        if (sample)
            Sleep(200);
        report << (sample ? "," : "") << "{\"begin_us\":" << micros();
        DWORD foreground_pid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foreground_pid);
        report << ",\"target_is_foreground\":"
               << (foreground_pid == parsed ? "true" : "false");
        for (const auto& item : catalogue) {
            report << ",\"" << item.name << "\":";
            try {
                auto relative = item;
                relative.va -= preferred_base;
                report << value_json(image, relative);
            } catch (const std::exception&) {
                report << "null";
                ++failures;
            }
        }
        report << ",\"end_us\":" << micros() << '}';
    }
    report << "],\"unreadable_scalars\":" << failures
           << ",\"physics_entry_is_completed_step\":false,"
              "\"world_simulation_validated\":false}\n";
    write_new(argv[6], report.str());
    std::cout << "Timing observed: 26 samples, " << catalogue.size()
              << " fixed scalars, " << failures
              << " unreadable; no simulation or focus change\n";
    return 0;
}
} // namespace

int wmain(int argc, wchar_t* argv[]) {
    try {
        return run(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
