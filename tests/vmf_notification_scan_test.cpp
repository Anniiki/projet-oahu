#include "tdur/ui/vmf_notification_scan.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {
using namespace tdur::ui;
std::size_t checks{}, failures{};
const std::string marker{"\xc2\xa7", 2};
std::string token(std::string_view body) {
    return marker + std::string(body) + marker;
}
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << message << '\n';
    }
}
void value(std::string_view source, std::int32_t expected,
           const char* message) {
    const std::string snapshot(source);
    const auto result = scan_vmf_notification_tokens(
        std::span<const char>{source.data(), source.size()});
    check(result && *result == expected, message);
    check(source == snapshot, "prepass preserves every source byte");
}
void error(std::string_view source, VmfNotificationScanErrorCode expected,
           std::size_t offset, const char* message) {
    const std::string snapshot(source);
    const auto result = scan_vmf_notification_tokens(
        std::span<const char>{source.data(), source.size()});
    check(!result && result.error().code == expected &&
              result.error().offset == offset,
          message);
    check(source == snapshot, "refusal preserves every source byte");
}
void ordering() {
    value({}, 0, "empty backing implies empty string");
    value("No parameters here", 0, "literal text needs no arguments");
    value(token("1") + token("2"), 2,
          "encountering1 then2 admits two arguments");
    value(token("2") + token("1"), -1,
          "2 then1 leaves a native gap, not a modern error");
    value(token("1") + token("1") + token("2"), 2,
          "duplicate earlier indices do not introduce a gap");
    value(token("2") + token("1") + token("2"), 2,
          "a later repeated2 can fill the earlier gap without restarting");
    value(token("1") + token("3") + token("2"), -1,
          "largest encountered index can exceed accepted progress");
    value(token("3") + token("1") + token("2") + token("3"), 3,
          "later occurrences satisfy encounter order");
    value(token("0") + token("-9") + token("10") + token("word"), 0,
          "zero negative oversized and named tokens need not fail prepass");
    value(token("9"), -1, "a lone9 records a gap");
    std::string all;
    for (int index = 1; index <= 10; ++index)
        all += token(std::to_string(index));
    value(all, 9, "signed parsed<=9 guard makes final clamp10 unreachable");
    std::array<char, 4> order{'1', '2', '3', '4'};
    bool first = true;
    do {
        std::string source;
        for (const auto index : order)
            source += token(std::string_view{&index, 1});
        value(source, first ? 4 : -1,
              "only ascending permutation reaches four accepted indices");
        first = false;
    } while (std::next_permutation(order.begin(), order.end()));
}
void decimal_prefixes() {
    value(token("1abc") + token("02suffix"), 2,
          "%d parses a prefix even when later formatter treats token as named");
    value(token(" \t\v\f\r\n+01"), 1,
          "ASCII whitespace sign and zeros are admitted");
    value(token("1") + token("-0") + token("-2147483648") + token("2"), 2,
          "full negative int32 domain is defined and does not advance count");
    value(token("2147483647"), 0,
          "largest positive int32 is parsed then ignored");
    value(
        token("+word") + token("-word") + token("") + token("1"), 1,
        "no decimal assignment differs from EOF and never uses unknown local");
    value(
        token(std::string(4096, '0') + "1"), 1,
        "long leading zeros do not inherit host converter's31-byte text limit");
    value(token(std::string(2048, ' ') + "+" + std::string(2048, '0') + "1"), 1,
          "leading whitespace and zeros have no native fixed token-size limit");
    value(token("-" + std::string(4096, '0')), 0,
          "long negative zero normalizes without overflow");
    error(token("2147483648"), VmfNotificationScanErrorCode::integer_range, 12,
          "positive conversion overflow is explicit modern refusal");
    error(token("-2147483649"), VmfNotificationScanErrorCode::integer_range, 13,
          "negative conversion overflow is explicit modern refusal");
    error(token(std::string(40, '9')),
          VmfNotificationScanErrorCode::integer_range, 13,
          "unbounded significant digits are rejected within bounded input");
}
void byte_traversal() {
    value(std::string{"\xf0", 1} + token("1"), 1,
          "F0 uses a one-byte step, not a four-byte Unicode sequence");
    value(std::string{"\xff\x80", 2} + token("1"), 1,
          "other high-byte patterns also use one-byte steps");
    value(std::string{"\xc0\0", 2} + token("1"), 1,
          "two-byte step skips internal NUL without validating continuation");
    value(std::string{"\xe0\0X", 3} + token("1"), 1,
          "three-byte step skips internal NUL without Unicode validation");
    value(std::string{"\xc2X", 2} + token("1"), 1,
          "C2 alone is not the delimiter");
    value(std::string{"\xf0\0", 2} + token("1"), 0,
          "one-byte step encounters and stops at the following NUL");
    value(std::string{"\0", 1} + marker, 0,
          "outer NUL stops before a malformed trailing marker");
    error(std::string{"\xc2", 1}, VmfNotificationScanErrorCode::source_range, 0,
          "truncated two-byte leading character is refused before advancing");
    error(std::string{"\xe0X", 2}, VmfNotificationScanErrorCode::source_range,
          0, "truncated three-byte leading character is refused");
    error(marker + "name", VmfNotificationScanErrorCode::source_range, 6,
          "missing closing marker cannot advance beyond implied terminal NUL");
    const auto crossing = marker + std::string{"1\0x", 3} + marker;
    value(crossing, 1,
          "inner closing search crosses supplied NUL unlike outer loop");
    const auto stale = marker + std::string{"\0", 1} + marker;
    error(stale, VmfNotificationScanErrorCode::unknown_scan_value, 2,
          "EOF before any assignment leaves native local unknown");
    error(marker, VmfNotificationScanErrorCode::unknown_scan_value, 2,
          "virtual terminal NUL also gives EOF with unknown initial local");
    value(token("1") + stale, 1, "EOF preserves previously assigned local1");
    value(token("2") + stale, -1, "EOF preserves previously assigned gap2");
    value(
        token("1") + token("2") + stale, 2,
        "EOF preserves latest assigned value rather than resetting temporary");
    const auto sign_eof = marker + std::string{"+\0", 2} + marker;
    error(sign_eof, VmfNotificationScanErrorCode::unknown_scan_value, 3,
          "historical CRT returns EOF for sign immediately followed by NUL");
    error(
        marker + "-", VmfNotificationScanErrorCode::unknown_scan_value, 3,
        "minus before virtual terminal NUL also leaves initial local unknown");
    value(token("1") + sign_eof, 1,
          "sign before NUL retains known scanf temporary after historical EOF");
    value(marker + std::string{"+ \0", 3} + marker, 0,
          "space after sign produces zero assignments rather than EOF");
    error(token("name") + stale,
          VmfNotificationScanErrorCode::unknown_scan_value,
          token("name").size() + 2,
          "a failed decimal match does not initialize the scanf local");
    value(token("1") + token("name") + stale, 1,
          "zero assignments do not erase a known scanf temporary");
    value(marker + std::string{"1\0", 2} + marker + token("2"), 2,
          "after closing marker scanner resumes beyond earlier inner NUL");
}
} // namespace

int main() {
    ordering();
    decimal_prefixes();
    byte_traversal();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
