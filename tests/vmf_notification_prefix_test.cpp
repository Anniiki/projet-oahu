#include "tdur/ui/vmf_notification_prefix.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {
using namespace tdur::ui;
std::size_t checks{}, failures{};
void check(bool condition, const char* text) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << text << '\n';
    }
}
void compare(std::optional<std::string_view> reference,
             std::optional<std::string_view> candidate, std::uint32_t count,
             std::int32_t expected, const char* text) {
    const auto result =
        compare_vmf_notification_prefix(reference, candidate, count);
    check(result && *result == expected, text);
}
void comparison() {
    using Error = VmfNotificationPrefixErrorCode;
    compare("A", "Z", 1, 1,
            "mismatch uses reference presence, not byte ordering");
    compare("Z", "A", 1, 1, "reversing mismatch can still return positive1");
    compare("", "", 1, -1, "two empty strings do not consume positive count");
    compare("", std::nullopt, 1, -1, "empty reference bypasses candidate read");
    compare("", std::nullopt, 0, 0,
            "empty reference with zero count needs no candidate");
    compare("A", "", 0, 0, "candidate NUL precedes zero-count result");
    compare("A", "B", 0, 0,
            "zero count returns equality even for differing bytes");
    compare("A", "A", 1, 0, "one consumed match yields zero");
    compare("A", "A", 2, -1,
            "reference terminator reached before remaining count");
    compare("AB", "A", 2, 1,
            "candidate terminator with live reference yields1");
    compare("AB", "A", 1, 0,
            "extra reference/candidate reads do not change consumed result");
    compare(std::string_view{"A\0B", 3}, "AB", 2, -1,
            "embedded reference NUL terminates comparison");
    compare("AB", std::string_view{"A\0B", 3}, 2, 1,
            "embedded candidate NUL retains nonempty reference sign");
    compare("AB", "AB", std::numeric_limits<std::uint32_t>::max(), -1,
            "large unsigned count terminates at bounded reference NUL");
    const std::string marker{"\xc2\xa7", 2};
    compare(marker, marker, 2, 0, "real two-byte delimiter consumes its count");
    compare(marker, marker, 3, -1,
            "three-byte step cannot match two-byte delimiter");
    compare(std::string{"\xff", 1}, std::string{"\xff", 1}, 1, 0,
            "high bytes compare raw without case or locale conversion");
    for (const auto count : {0U, 1U}) {
        const auto missing =
            compare_vmf_notification_prefix(std::nullopt, "", count);
        check(!missing && missing.error().code == Error::missing_reference &&
                  missing.error().offset == 0,
              "reference read is required even when count is zero");
    }
    const auto candidate =
        compare_vmf_notification_prefix("A", std::nullopt, 0);
    check(!candidate && candidate.error().code == Error::missing_candidate,
          "nonempty reference requires candidate before zero-count test");
}
void copying() {
    using Error = VmfNotificationPrefixErrorCode;
    std::array<char, 8> output;
    output.fill('X');
    auto result = copy_vmf_notification_prefix(output, "ABC", 7);
    check(result && *result == 3 &&
              std::string(output.data(), output.size()) == "ABCXXXXX",
          "copy does not write source terminator or pad destination");
    output.fill('X');
    result = copy_vmf_notification_prefix(output, "ABC", 2);
    check(result && *result == 2 &&
              std::string(output.data(), output.size()) == "ABXXXXXX",
          "count clips copy without appending NUL");
    result = copy_vmf_notification_prefix({}, std::nullopt, 0);
    check(result && *result == 0,
          "zero count never needs source or destination");
    result = copy_vmf_notification_prefix({}, "", 99);
    check(result && *result == 0,
          "source NUL requires no writable destination");
    result = copy_vmf_notification_prefix({}, std::nullopt, 1);
    check(!result && result.error().code == Error::missing_source &&
              result.error().offset == 0,
          "positive count needs source before any destination access");
    result = copy_vmf_notification_prefix({}, "A", 1);
    check(!result && result.error().code == Error::output_range &&
              result.error().offset == 0,
          "nonempty source refuses first out-of-range store");
    output.fill('X');
    result = copy_vmf_notification_prefix(std::span<char>{output}.first(2),
                                          "ABCD", 4);
    check(
        !result && result.error().code == Error::output_range &&
            result.error().offset == 2 &&
            std::string(output.data(), output.size()) == "ABXXXXXX",
        "short destination refusal retains earlier stores and exact progress");
    output.fill('X');
    result =
        copy_vmf_notification_prefix(output, std::string_view{"A\0B", 3}, 7);
    check(result && *result == 1 && output[0] == 'A' && output[1] == 'X',
          "embedded source NUL is observed but never written");
    result = copy_vmf_notification_prefix(output, std::string_view{"AB", 2}, 7);
    check(result && *result == 2 && output[2] == 'X',
          "view end provides the implied NUL");
    std::array<char, 8> overlap{'A', 'B', 'C', 'D', 0, 'x', 'y', 'z'};
    result =
        copy_vmf_notification_prefix(std::span<char>{overlap}.subspan(1),
                                     std::string_view{overlap.data(), 5}, 5);
    check(result && *result == 5 &&
              std::string(overlap.data(), overlap.size()) == "AAAAAAyz",
          "forward right-overlap propagates stores instead of snapshot or "
          "memmove");
    overlap = {'A', 'B', 'C', 'D', 0, 'x', 'y', 'z'};
    result = copy_vmf_notification_prefix(
        overlap, std::string_view{overlap.data() + 1, 4}, 7);
    check(result && *result == 3 && std::string(overlap.data(), 4) == "BCDD" &&
              overlap[4] == 0,
          "forward left-overlap keeps source NUL and untouched tail");
    overlap = {'A', 'B', 'C', 'D', 0, 'x', 'y', 'z'};
    const auto before = overlap;
    result = copy_vmf_notification_prefix(
        overlap, std::string_view{overlap.data(), 5}, 7);
    check(result && *result == 4 && overlap == before,
          "exact self-copy is defined and preserves NUL");
    output.fill('X');
    result =
        copy_vmf_notification_prefix(output, std::string{"\xff\x80", 2}, 7);
    check(result && *result == 2 &&
              static_cast<unsigned char>(output[0]) == 0xff &&
              static_cast<unsigned char>(output[1]) == 0x80 && output[2] == 'X',
          "copy treats high bytes as opaque payload");
}
} // namespace

int main() {
    comparison();
    copying();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
