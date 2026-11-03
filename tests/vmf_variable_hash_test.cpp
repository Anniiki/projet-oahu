#include "tdur/ui/vmf_variable_hash.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {
using tdur::ui::vmf_variable_name_hash;
using tdur::ui::VmfVariableHashError;
std::size_t checks = 0;
std::size_t failures = 0;

static_assert(noexcept(vmf_variable_name_hash(0, {}, 0)));
static_assert(
    std::is_same_v<decltype(vmf_variable_name_hash(0, {}, 0)),
                   std::expected<std::uint32_t, VmfVariableHashError>>);

void check(bool passed, std::string_view label) {
    ++checks;
    if (!passed) {
        ++failures;
        std::cerr << "FAIL: " << label << '\n';
    }
}

void expect(std::uint32_t scope, std::string_view name, std::uint32_t capacity,
            std::uint32_t expected, std::string_view label) {
    const auto result = vmf_variable_name_hash(scope, name, capacity);
    check(result && *result == expected, label);
}

void fixed_vectors() {
    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    expect(0, {}, 0, 0, "empty view does not divide");
    expect(maximum, {}, 0, 0, "empty ignores seed and zero capacity");
    expect(7, {}, 97, 0, "empty bypasses the scope seed");
    expect(7, std::string_view("\0tail", 5), 0, 0,
           "leading NUL also bypasses division");
    const auto invalid = vmf_variable_name_hash(0, "A", 0);
    check(!invalid && invalid.error() == VmfVariableHashError::zero_capacity,
          "nonempty zero capacity explicitly refused");
    expect(0, "A", 97, 65, "single unsigned byte");
    expect(7, "a", 97, 72, "scope and ASCII fold");
    expect(maximum, "A", maximum, 64, "seed addition wraps before modulo");
    expect(maximum, "A", 97, 64, "wrapped dividend is unsigned");
    expect(0, "abcd", 97, 28, "word44434241 little-endian");
    expect(0x01020304, "ABCD", 97, 48, "scope adds to whole word");
    expect(0, "abcde", 97, 60, "partial word retains high bytes");
    expect(0, "abcdef", 97, 17, "two-byte tail keeps earlier high bytes");
    expect(0, "abcdefgh", 97, 63, "two full words");
    expect(0, "abcdefghi", 97, 5, "tail after two full words");
    expect(0, std::string_view("abcd\0efgh", 9), 97, 28,
           "NUL after full group adds no phantom tail");
    expect(0, std::string_view("ab\0c", 4), 97, 83,
           "NUL in first partial group ignores suffix");
    expect(0, "[", 97, 91, "punctuation literal");
    expect(0, "{", 97, 91, "DF folds punctuation too");
    expect(7, "    ", 97, 7, "masked spaces are not input terminators");
    expect(0, "a b", 97, 17, "zero masked byte does not end packing");
    expect(0, std::string_view("\xff\x80\xe0\xff", 4), 97, 54,
           "high bytes do not sign-extend");
    expect(123, "anything", 1, 0, "capacity one");

    // Independent cases002/04-05: names are hashed before storage truncation.
    const std::string prefix(31, 'V');
    expect(1, prefix, 4, 1, "31-byte stored prefix");
    expect(1, prefix + "A", 2, 1, "32-byte same-bucket case");
    expect(1, prefix + "BA", 4, 2, "33-byte divergent full-name bucket");

    // Dictionary oracle002/07 topology depends on these exact home buckets.
    expect(2, "C", 4, 1, "oracle scope2 C");
    expect(1, "A", 4, 2, "oracle scope1 A");
    expect(2, "D", 4, 2, "oracle scope2 D");
    expect(1, "E", 4, 2, "oracle scope1 E");
    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        const std::array<char, 1> upper{letter};
        const std::array<char, 1> lower{static_cast<char>(letter + 32)};
        const auto a = vmf_variable_name_hash(0, {upper.data(), 1}, 257);
        const auto b = vmf_variable_name_hash(0, {lower.data(), 1}, 257);
        check(a && b && *a == *b, "ASCII upper/lower invariance");
    }
}

void bounded_views() {
#if defined(_WIN32)
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const auto page = static_cast<std::size_t>(info.dwPageSize);
    auto *base = static_cast<char *>(VirtualAlloc(
        nullptr, page * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    check(base != nullptr, "guard fixture allocation");
    if (!base)
        return;
    DWORD previous = 0;
    const bool protected_page =
        VirtualProtect(base + page, page, PAGE_NOACCESS, &previous) != 0;
    check(protected_page, "guard fixture protection");
    if (protected_page) {
        // Literal word sums: tail bytes remain58 after the first full word.
        constexpr std::array<std::uint32_t, 13> sums{
            0,          0x58,       0x5858,     0x585858,   0x58585858,
            0xb0b0b0b0, 0xb0b0b0b0, 0xb0b0b0b0, 0xb0b0b0b0, 0x09090908,
            0x09090908, 0x09090908, 0x09090908};
        for (std::size_t length = 0; length < sums.size(); ++length) {
            auto *first = base + page - length;
            for (std::size_t i = 0; i < length; ++i)
                first[i] = 'x';
            expect(0, {first, length},
                   std::numeric_limits<std::uint32_t>::max(), sums[length],
                   "unterminated view at PAGE_NOACCESS boundary");
        }
    }
    check(VirtualFree(base, 0, MEM_RELEASE) != 0, "guard fixture release");
#endif
}
} // namespace

int main() {
    fixed_vectors();
    bounded_views();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
