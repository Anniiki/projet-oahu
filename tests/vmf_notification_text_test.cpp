#include "tdur/ui/vmf_notification_text.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
using namespace tdur::ui;
std::size_t checks{}, failures{};
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << message << '\n';
    }
}
template <class Result>
bool error_is(const Result& result, VmfNotificationTextError error) {
    return !result && result.error() == error;
}
void encoder() {
    std::array<char, 12> output;
    output.fill('Z');
    const std::array<char, 2> suffix{static_cast<char>(0xa7), '\0'};
    auto result = encode_vmf_notification_bytes(output, suffix, 1);
    check(result && result->consumed == 1 && result->written == 3 &&
              result->terminated &&
              static_cast<unsigned char>(output[0]) == 0xc2 &&
              static_cast<unsigned char>(output[1]) == 0xa7 && output[2] == 0 &&
              output[3] == 'Z',
          "A7 expands to raw C2A7 plus NUL; positive guard is not capacity");
    const auto unchanged = output;
    for (const auto guard : {0, -1, INT32_MIN}) {
        result = encode_vmf_notification_bytes(output, {}, guard);
        check(result && result->written == 0 && output == unchanged,
              "nonpositive guard needs no source or output");
    }
    const std::array<char, 4> ascii{'A', 'B', 'C', '\0'};
    for (const auto limit : {0, -2, INT32_MIN}) {
        result = encode_vmf_notification_bytes(output, ascii, 5, limit);
        check(result && result->consumed == 0 && result->written == 0 &&
                  !result->terminated && output == unchanged,
              "signed nonpositive source limit other than -1 writes no NUL");
    }
    output.fill('Z');
    result = encode_vmf_notification_bytes(output, ascii, 7, 2);
    check(result && result->written == 2 && result->consumed == 2 &&
              !result->terminated && output[0] == 'A' && output[1] == 'B' &&
              output[2] == 'Z',
          "exact source limit omits terminator");
    result = encode_vmf_notification_bytes(output, ascii, 7, 3);
    check(result && result->written == 3 && !result->terminated &&
              output[3] == 'Z',
          "NUL found exactly at source limit is not copied");
    result = encode_vmf_notification_bytes(output, ascii, 7, 4);
    check(result && result->written == 4 && result->terminated &&
              output[3] == 0,
          "NUL before source limit counts in native return");
    const std::array<char, 1> empty{'\0'};
    result = encode_vmf_notification_bytes(output, empty, 8, 0);
    check(result && result->written == 0 && !result->terminated,
          "empty input at zero limit has no output NUL");
    result = encode_vmf_notification_bytes(output, empty, 8, -1);
    check(result && result->written == 1 && result->consumed == 0 &&
              result->terminated && output[0] == 0,
          "empty unbounded input returns one for NUL");
    output.fill('Z');
    const auto before = output;
    result =
        encode_vmf_notification_bytes(std::span(output).first(2), suffix, 8);
    check(error_is(result, VmfNotificationTextError::output_range) &&
              output == before,
          "modern output bound rejects before writes");
    result = encode_vmf_notification_bytes(output, {}, 8, 0);
    check(error_is(result, VmfNotificationTextError::source_range) &&
              output == before,
          "positive guard still requires initial source byte at limit zero");
    result =
        encode_vmf_notification_bytes(output, std::span(ascii).first(2), 8, 2);
    check(error_is(result, VmfNotificationTextError::source_range) &&
              output == before,
          "following-byte native read must be covered even at exact limit");
    std::array<char, 8> alias{'A', '\0', 'Z', 'Z', 'Z', 'Z', 'Z', 'Z'};
    const auto alias_before = alias;
    result = encode_vmf_notification_bytes(alias, std::span(alias).first(2), 8);
    check(error_is(result, VmfNotificationTextError::overlapping_storage) &&
              alias == alias_before,
          "modern alias refusal preserves source and output");
    const std::array<char, 3> already_utf8{static_cast<char>(0xc2),
                                           static_cast<char>(0xa7), '\0'};
    result = encode_vmf_notification_bytes(output, already_utf8, 8);
    const std::array<char, 5> expanded{
        static_cast<char>(0xc3), static_cast<char>(0x82),
        static_cast<char>(0xc2), static_cast<char>(0xa7), '\0'};
    check(result && result->written == 5 &&
              std::equal(expanded.begin(), expanded.end(), output.begin()),
          "input UTF8 bytes expand separately; no normalization");
#ifdef _WIN32
    // Independent Windows UTF8 encoder supplies the expected bytes, without
    // repeating the production shifts/masks. Each input byte maps to its
    // same-valued Unicode codepoint, as proved in the native helper.
    for (unsigned code = 1; code <= 255; ++code) {
        const std::array<char, 2> source{static_cast<char>(code), '\0'};
        const std::array<wchar_t, 2> wide{static_cast<wchar_t>(code), L'\0'};
        std::array<char, 8> expected{};
        const auto written = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), 2, expected.data(),
            static_cast<int>(expected.size()), nullptr, nullptr);
        result = encode_vmf_notification_bytes(output, source, 1);
        check(written > 0 && result &&
                  result->written == static_cast<std::size_t>(written) &&
                  std::equal(expected.begin(), expected.begin() + written,
                             output.begin()),
              "all nonzero bytes match independent Unicode-to-UTF8 oracle");
    }
#endif
}
void parameters() {
    using Status = VmfNotificationTextBindStatus;
    VmfNotificationTextParameters table;
    auto result = bind_vmf_notification_text(nullptr, std::nullopt, "x");
    check(result && result->status == Status::ignored,
          "NULL name guard precedes NULL context");
    result = bind_vmf_notification_text(nullptr, "x", std::nullopt);
    check(result && result->status == Status::ignored,
          "NULL value guard precedes NULL context");
    for (const auto name : {std::string_view{}, std::string_view{"\0x", 2}}) {
        result = bind_vmf_notification_text(nullptr, name, "");
        check(result && result->status == Status::ignored,
              "empty C-string name avoids context access");
    }
    result = bind_vmf_notification_text(nullptr, "x", "");
    check(error_is(result, VmfNotificationTextError::invalid_context),
          "known NULL context with valid arguments is not a no-op");
    for (auto& slot : table.slots) {
        slot.key.fill('Z');
        slot.value.fill('Y');
        slot.key[0] = 0;
        slot.value[0] = 0;
    }
    result = bind_vmf_notification_text(&table, "NB_REQ", "3");
    check(result && result->status == Status::inserted && result->slot == 0U,
          "first empty slot selected");
    const std::array<char, 9> key{'N',
                                  'B',
                                  '_',
                                  'R',
                                  'E',
                                  'Q',
                                  static_cast<char>(0xc2),
                                  static_cast<char>(0xa7),
                                  '\0'};
    check(std::equal(key.begin(), key.end(), table.slots[0].key.begin()) &&
              table.slots[0].key[9] == 'Z',
          "raw suffix and terminator written; unused key tail preserved");
    check(table.slots[0].value[0] == '3' &&
              std::all_of(table.slots[0].value.begin() + 1,
                          table.slots[0].value.end(),
                          [](char c) { return c == 0; }),
          "short values use complete strncpy padding");
    const auto first = table.slots[0];
    result = bind_vmf_notification_text(&table, "NB_REQ", "8");
    check(result && result->slot == 1U && table.slots[0] == first &&
              table.slots[1].value[0] == '8',
          "duplicate key consumes next empty slot without updating first");
    result = bind_vmf_notification_text(&table, "ABCDEFGHIJKLMNO", "");
    check(result && result->slot == 2U && table.slots[2].key[17] == 0 &&
              static_cast<unsigned char>(table.slots[2].key[15]) == 0xc2 &&
              static_cast<unsigned char>(table.slots[2].key[16]) == 0xa7,
          "fifteen-byte name exactly fits suffix and NUL");
    auto before = table;
    result = bind_vmf_notification_text(&table, "ABCDEFGHIJKLMNOP", "x");
    check(error_is(result, VmfNotificationTextError::name_limit) &&
              table == before,
          "sixteen-byte name is explicit modern error without writes");
    result =
        bind_vmf_notification_text(&table, std::string_view{"K\0unread", 8},
                                   std::string_view{"V\0tail", 6});
    check(result && result->slot == 3U && table.slots[3].key[0] == 'K' &&
              static_cast<unsigned char>(table.slots[3].key[1]) == 0xc2 &&
              table.slots[3].value[0] == 'V' && table.slots[3].value[1] == 0,
          "embedded NULs end both supplied C-string prefixes");
    const std::string long_value(80, 'L');
    result = bind_vmf_notification_text(&table, "LONG", long_value);
    check(result && result->slot == 4U &&
              std::all_of(table.slots[4].value.begin(),
                          table.slots[4].value.begin() + 63,
                          [](char c) { return c == 'L'; }) &&
              table.slots[4].value[63] == 0,
          "long value keeps63 bytes and forced final NUL");
    before = table;
    result = bind_vmf_notification_text(
        &table, "ALIAS", std::string_view{table.slots[5].key.data(), 18});
    check(
        error_is(result, VmfNotificationTextError::overlapping_storage) &&
            table == before,
        "value initially empty in selected key still aliases later name write");
    result = bind_vmf_notification_text(
        &table, std::string_view{table.slots[0].key.data(), 6}, "OTHER");
    check(result && result->slot == 5U && table.slots[0] == first,
          "input from a distinct slot is valid and not overwritten");
    for (std::size_t i = 6; i < 10; ++i) {
        result = bind_vmf_notification_text(&table, "FILL", "1");
        check(result && result->slot == i, "all ten slots usable");
    }
    before = table;
    result =
        bind_vmf_notification_text(&table, "NAME_TOO_LONG_FOR_BUFFER", "x");
    check(result && result->status == Status::full && !result->slot &&
              table == before,
          "full table is no-op before modern name-length validation");
    table.slots[7].key[0] = 0;
    result = bind_vmf_notification_text(&table, "NB_REQ", "HOLE");
    check(result && result->slot == 7U && table.slots[0] == first,
          "first available hole used even with existing matching name");
    before = table;
    clear_vmf_notification_text(table);
    for (std::size_t i = 0; i < 10; ++i) {
        check(table.slots[i].key[0] == 0 && table.slots[i].value[0] == 0,
              "clear empties both first bytes in all slots");
        check(std::equal(table.slots[i].key.begin() + 1,
                         table.slots[i].key.end(),
                         before.slots[i].key.begin() + 1) &&
                  std::equal(table.slots[i].value.begin() + 1,
                             table.slots[i].value.end(),
                             before.slots[i].value.begin() + 1),
              "clear preserves every remaining key/value byte");
    }
    result = bind_vmf_notification_text(&table, "NEW", "");
    check(result && result->slot == 0U && table.slots[0].value[63] == 0,
          "cleared table immediately reusable with an empty value");
}
} // namespace
int main() {
    encoder();
    parameters();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
