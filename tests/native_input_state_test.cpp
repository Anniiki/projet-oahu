#include "fixtures/native_input_cases.hpp"
#include "tdur/input/native_input_state.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
using namespace tdur::input;
std::size_t checks = 0;
void Check(bool condition, const char *message) {
    ++checks;
    if (!condition)
        throw std::runtime_error(message);
}
static_assert(sizeof(NativeInputRecord) == 84);
static_assert(std::is_trivially_copyable_v<NativeInputRecord>);
static_assert(noexcept(find_native_input_slot({}, 0, 0)));
static_assert(noexcept(reset_native_input_records({})));

void Oracles() {
    for (const auto &test : native_input_cases::lookups)
        Check(find_native_input_slot(std::span(test.slots).first(test.count),
                                     test.id, test.ordinal) == test.expected,
              "Frozen native lookup case");
    for (const auto &test : native_input_cases::resets) {
        std::array<NativeInputRecord, 3> records;
        for (std::size_t r = 0; r < records.size(); ++r)
            for (std::size_t b = 0; b < 84; ++b)
                records[r].bytes[b] = std::byte{test.before[r][b]};
        reset_native_input_records(std::span(records).first(test.count));
        for (std::size_t r = 0; r < records.size(); ++r)
            for (std::size_t b = 0; b < 84; ++b)
                Check(std::to_integer<unsigned>(records[r].bytes[b]) ==
                          test.after[r][b],
                      "Frozen reset bytes/canaries/outside-count record");
        const auto once = records;
        reset_native_input_records(std::span(records).first(test.count));
        for (std::size_t r = 0; r < records.size(); ++r)
            Check(records[r].bytes == once[r].bytes,
                  "Reset is idempotent across full record");
    }
    const std::array<std::optional<std::uint32_t>, 3> slots{0, 0, 0};
    Check(!find_native_input_slot(slots, 0,
                                  std::numeric_limits<std::uint32_t>::max()),
          "Maximum ordinal does not alias first match");
    const std::array<std::optional<std::uint32_t>, 3> stopped{0, {}, 0};
    Check(!find_native_input_slot(stopped, 0, 1),
          "NULL stops ordinal scan after an earlier matching ID");
    reset_native_input_records({});
}

void FlagDomain() {
    // Every pair of flag bytes, with fixed canaries and raw sNaN bits. The
    // expected whole record follows the frozen native masks, not a float cast.
    for (unsigned a = 0; a != 256; ++a) {
        for (unsigned b = 0; b != 256; ++b) {
            NativeInputRecord record;
            record.bytes.fill(std::byte{0xa7});
            record.bytes[8] = std::byte{static_cast<unsigned char>(a)};
            record.bytes[9] = std::byte{static_cast<unsigned char>(b)};
            record.bytes[48] = std::byte{1};
            record.bytes[49] = std::byte{0};
            record.bytes[50] = std::byte{0x80};
            record.bytes[51] = std::byte{0x7f};
            auto expected = record.bytes;
            expected[8] = std::byte{static_cast<unsigned char>(a % 16)};
            expected[9] = std::byte{static_cast<unsigned char>(b - b % 2)};
            std::fill(expected.begin() + 48, expected.begin() + 52,
                      std::byte{});
            reset_native_input_records({&record, 1});
            Check(record.bytes == expected, "Exhaustive flag masks/canaries");
        }
    }
}

#ifdef _WIN32
void GuardPages() {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const auto page = static_cast<std::size_t>(system.dwPageSize);
    auto *memory = static_cast<std::byte *>(VirtualAlloc(
        nullptr, 2 * page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Check(memory != nullptr, "Guard page allocation");
    struct Release {
        void *value;
        ~Release() { VirtualFree(value, 0, MEM_RELEASE); }
    } release{memory};
    DWORD previous{};
    Check(VirtualProtect(memory + page, page, PAGE_NOACCESS, &previous) != 0,
          "Guard page protection");
    using Slot = std::optional<std::uint32_t>;
    auto *slot = reinterpret_cast<Slot *>(memory + page - 2 * sizeof(Slot));
    std::construct_at(slot, 5U);
    std::construct_at(slot + 1, 0U);
    Check(!find_native_input_slot({slot, 2}, 42, 0),
          "Missing lookup does not prefetch a third slot");
    Check(find_native_input_slot({slot, 2}, 0, 0) == std::size_t{1},
          "Last valid slot selected beside unreadable page");
    Check(!find_native_input_slot({slot + 2, 0}, 0, 0),
          "Empty span does not read its inaccessible base");
    std::destroy_n(slot, 2);
    auto *record = reinterpret_cast<NativeInputRecord *>(memory + page - 84);
    std::construct_at(record);
    record->bytes.fill(std::byte{0xff});
    reset_native_input_records({record, 1});
    Check(record->bytes[83] == std::byte{0xff},
          "Reset last record does not touch inaccessible successor");
    std::destroy_at(record);
}
#endif
} // namespace

int main() {
    try {
        Oracles();
        FlagDomain();
#ifdef _WIN32
        GuardPages();
#endif
        std::cout << "native_input_state: " << checks << " checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
