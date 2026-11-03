#include "tdur/assets/resource_name_hash.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
using namespace tdur::assets;
std::size_t checks{}, failures{};
void check(bool condition, const char* label) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cerr << label << '\n';
    }
}
std::span<const std::byte> bytes(std::string_view value) {
    return std::as_bytes(std::span<const char>{value.data(), value.size()});
}
void vectors() {
    // Independent native-register/table oracles.
    check(hash_resource_name("testcar_01", 1) == 0x5f52414354537684ULL,
          "legacy golden lower ASCII and lane wrap");
    check(hash_resource_name("TESTCAR_01", 1) == 0x5f52414354537684ULL,
          "legacy folds only ASCII lower letters");
    check(hash_resource_name("testcar_01", 0) == 0x88dfbd66fcedeff4ULL,
          "mode0 reuses real backend golden lower");
    check(hash_resource_name("TESTCAR_01", 0) == 0xa8218072d225b0caULL,
          "mode0 preserves ASCII case");
    const std::array<unsigned char, 19> high{
        0x61, 0x6c, 0x70, 0x68, 0x61, 0x80, 0xff, 0xfe, 0x5f, 0x65,
        0x78, 0x74, 0x72, 0x61, 0x5f, 0x77, 0x72, 0x61, 0x70};
    const auto raw = std::as_bytes(std::span{high});
    check(hash_legacy_resource_bytes(raw, 19) == 0x555ec1939cf8d2f2ULL,
          "legacy raw high bytes remain unchanged and add modulo256");
    check(hash_resource_bytes(raw, 19) == 0x64c3526a074f99efULL,
          "existing backend golden high bytes");
    const auto name = std::string_view{"prefix\0hidden_suffix", 20};
    check(hash_resource_name(name, 1) == 0x0000584946455250ULL,
          "legacy wrapper stops at first NUL");
    check(hash_resource_name(name, 0) == 0xb94772f38193e379ULL,
          "backend wrapper stops at first NUL");
    check(hash_legacy_resource_bytes(bytes(name), 20) == 0x9d53b797e3d2dcdfULL,
          "raw legacy count includes NUL and following bytes");
    check(hash_resource_bytes(bytes(name), 20) == 0x27b5d4914ea0bd9bULL,
          "raw backend count includes NUL and following bytes");
}
void boundaries() {
    for (const auto count : {0, -1, std::numeric_limits<std::int32_t>::min()})
        check(hash_legacy_resource_bytes({}, count) == 0,
              "nonpositive legacy count returns zero on empty storage");
    check(hash_resource_name({}, 0) == resource_hash_seed,
          "empty backend name returns its real seed");
    check(hash_resource_name({}, 1) == 0,
          "empty legacy name returns zero rather than backend seed");
    for (const auto mode :
         {std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{255}})
        check(hash_resource_name("testcar_01", mode) == 0x5f52414354537684ULL,
              "every nonzero mode selects legacy, not just1");
    check(hash_resource_name("abcdefgh", 1) == 0x4847464544434241ULL,
          "eight lanes pack little endian as EDX:EAX");
    check(hash_resource_name("abcdefghi", 1) == 0x484746454443428aULL,
          "ninth byte returns to lane0");
    check(hash_resource_name("abcdefghabcdefgh", 1) == 0x908e8c8a88868482ULL,
          "lane additions do not carry into adjacent bytes");
    const std::array<unsigned char, 16> wrap{
        255, 255, 255, 255, 255, 255, 255, 255, 1, 1, 1, 1, 1, 1, 1, 1};
    check(hash_legacy_resource_bytes(std::as_bytes(std::span{wrap}), 16) == 0,
          "each lane independently wraps255plus1 to0");
    check(hash_legacy_resource_bytes(bytes("abcignored"), 3) == 0x434241ULL,
          "positive raw count consumes only prefix");
    check(hash_resource_name("a/b", 1) != hash_resource_name("a\\b", 1),
          "slash and backslash remain different");
    check(hash_resource_name(std::string_view{"\0ignored", 8}, 0) ==
              resource_hash_seed,
          "leadingNUL selects empty backend prefix");
    const auto short_span = hash_legacy_resource_bytes(bytes("abc"), 4);
    check(!short_span &&
              short_span.error() == ResourceHashError::count_exceeds_input,
          "positive short span reports error without overrun");
    const auto huge_count = hash_legacy_resource_bytes(
        {}, std::numeric_limits<std::int32_t>::max());
    check(!huge_count &&
              huge_count.error() == ResourceHashError::count_exceeds_input,
          "maximum signed positive count cannot wrap to empty success");
}
} // namespace
int main() {
    vectors();
    boundaries();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
