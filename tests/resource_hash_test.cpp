#include "tdur/assets/resource_hash.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

using tdur::assets::ResourceHashError;
using tdur::assets::hash_resource_bytes;
using tdur::assets::resource_hash_key_bytes;
using Bytes = std::vector<std::byte>;

std::size_t checks = 0;
std::size_t failures = 0;

void check(bool passed, std::string_view message) {
    ++checks;
    if (!passed) {
        ++failures;
        if (failures <= 24) {
            std::cerr << "FAIL: " << message << '\n';
        }
    }
}

// Independent bit-serial oracle, not the production table recurrence.
// Secondary 504980 assembly: ev_e27aa9611e14d83ee9b4ce1409866959a864aa8efc07b37a666210fbe5d4492c.
// Observed table: ev_ac251e8fb01bf1a24bcde846cb5d09fea2c8aead145325bfbd1890095da59b8a.
// All 256 entries were independently checked against the polynomial in the
// private evidence lot. This is mathematical metadata, not a copied table.
// Logical state stores native EAX as high32 and native EDX as low32. Each
// incoming byte is XORed AFTER eight feedback shifts, with no final XOR.
std::uint64_t serial_oracle(std::span<const std::byte> input) {
    std::uint64_t logical = 0xFAC432B10CD5E44AULL;
    for (const auto byte : input) {
        for (unsigned int bit = 0; bit < 8; ++bit) {
            const bool carry = (logical >> 63U) != 0;
            logical <<= 1U;
            if (carry) {
                logical ^= 0x0060034000F0D50BULL;
            }
        }
        logical ^= std::to_integer<std::uint64_t>(byte);
    }
    return (logical << 32U) | (logical >> 32U);
}

void expect_hash(std::span<const std::byte> input, std::int32_t count,
                 std::uint64_t expected, std::string_view message) {
    const auto result = hash_resource_bytes(input, count);
    check(result.has_value(), message);
    if (result) {
        check(*result == expected, message);
    }
}

void expect_full(std::span<const std::byte> input, std::uint64_t expected,
                 std::string_view message) {
    const auto result = hash_resource_bytes(input);
    check(result.has_value(), message);
    if (result) {
        check(*result == expected, message);
    }
}

void expect_error(std::span<const std::byte> input, std::int32_t count,
                  ResourceHashError expected, std::string_view message) {
    const auto result = hash_resource_bytes(input, count);
    check(!result.has_value(), message);
    if (!result) {
        check(result.error() == expected, message);
    }
}

Bytes text_bytes(std::string_view input) {
    Bytes output;
    output.reserve(input.size());
    for (const char value : input) {
        output.push_back(std::byte(static_cast<unsigned char>(value)));
    }
    return output;
}

void test_fixed_vectors() {
    check(tdur::assets::resource_hash_seed == 0x0CD5E44AFAC432B1ULL,
          "native EDX:EAX seed");
    expect_full({}, 0x0CD5E44AFAC432B1ULL, "empty seed");
    const std::array single_vectors = {
        std::pair{std::byte{0x00}, 0x86CB7EDEE5F3818CULL},
        std::pair{std::byte{0xFF}, 0x86CB7E21E5F3818CULL},
        std::pair{std::byte{0x01}, 0x86CB7EDFE5F3818CULL},
        std::pair{std::byte{0x80}, 0x86CB7E5EE5F3818CULL}};
    for (const auto& [byte, expected] : single_vectors) {
        const std::array input{byte};
        expect_full(input, expected, "fixed single-byte vector");
        check(serial_oracle(input) == expected, "single-byte oracle anchor");
    }
    const auto digits = text_bytes("123456789");
    expect_full(digits, 0xDCFB70C01B439313ULL, "fixed digits vector");
    check(serial_oracle(digits) == 0xDCFB70C01B439313ULL,
          "digits oracle anchor");
    const auto embedded_nul = text_bytes(std::string_view("ab\0cd", 5));
    expect_full(embedded_nul, 0x29A4D82F81B1F251ULL,
                "embedded NUL is hashed");
    const std::array mixed{std::byte{0}, std::byte{1}, std::byte{127},
                           std::byte{128}, std::byte{254}, std::byte{255},
                           std::byte{0}};
    expect_full(mixed, 0xD97F248BA3F2A9E3ULL, "mixed byte vector");
    Bytes ascending(256);
    for (std::size_t i = 0; i < ascending.size(); ++i) {
        ascending[i] = std::byte(i);
    }
    expect_full(ascending, 0x158A188BB5C0FE1EULL, "ascending256 anchor");
    Bytes long_pattern(4096);
    for (std::size_t i = 0; i < long_pattern.size(); ++i) {
        long_pattern[i] = std::byte((i * 37U + 11U) & 255U);
    }
    expect_full(long_pattern, 0x4FAE89C23C3D4350ULL,
                "4096-byte independent fixed vector");
}

void test_all_bytes_and_keys() {
    const std::array seed_key{std::byte{0xB1}, std::byte{0x32},
                              std::byte{0xC4}, std::byte{0xFA},
                              std::byte{0x4A}, std::byte{0xE4},
                              std::byte{0xD5}, std::byte{0x0C}};
    check(resource_hash_key_bytes(0x0CD5E44AFAC432B1ULL) == seed_key,
          "lookup key is EAX little-endian followed by EDX little-endian");
    const std::array sequence_key{std::byte{1}, std::byte{2}, std::byte{3},
                                  std::byte{4}, std::byte{5}, std::byte{6},
                                  std::byte{7}, std::byte{8}};
    check(resource_hash_key_bytes(0x0807060504030201ULL) == sequence_key,
          "lookup serializer does not swap DWORDs or bytes");
    for (unsigned int value = 0; value < 256; ++value) {
        const std::array input{std::byte(value)};
        const auto expected = serial_oracle(input);
        expect_full(input, expected, "all 256 input byte values");
        expect_hash(input, 1, expected, "single-byte signed-count overload");
        const auto key = resource_hash_key_bytes(expected);
        for (std::size_t i = 0; i < key.size(); ++i) {
            check(std::to_integer<std::uint64_t>(key[i]) ==
                      ((expected >> (8U * i)) & 255U),
                  "all-byte result serialized in native return order");
        }
    }
    for (unsigned int value = 0; value < 256; ++value) {
        for (std::size_t position = 0; position < 8; ++position) {
            std::array<std::byte, 8> key{};
            key[position] = std::byte(value);
            const auto marker = std::uint64_t{value} << (8U * position);
            check(resource_hash_key_bytes(marker) == key,
                  "each possible byte at each serializer position");
        }
    }
}

void test_counts_prefixes_offsets_and_canaries() {
    constexpr std::uint64_t seed = 0x0CD5E44AFAC432B1ULL;
    const std::array counts = {std::numeric_limits<std::int32_t>::min(),
                               std::int32_t{-100}, std::int32_t{-1},
                               std::int32_t{0}};
    const std::array filled{std::byte{0xA1}, std::byte{0xFF}, std::byte{0}};
    for (const auto count : counts) {
        expect_hash({}, count, seed, "nonpositive count with empty span");
        expect_hash(filled, count, seed,
                    "nonpositive count ignores a nonempty span");
    }
    expect_error({}, 1, ResourceHashError::count_exceeds_input,
                 "positive count refuses empty input");
    expect_error(filled, 4, ResourceHashError::count_exceeds_input,
                 "short positive prefix is refused");
    expect_error(filled, std::numeric_limits<std::int32_t>::max(),
                 ResourceHashError::count_exceeds_input,
                 "INT32_MAX never wraps or becomes a nonpositive prefix");

    for (std::size_t offset = 0; offset < 33; ++offset) {
        Bytes storage(offset + 257 + 37, std::byte{0xA5});
        for (std::size_t i = 0; i < 257; ++i) {
            storage[offset + i] = std::byte((i * 73U + offset) & 255U);
        }
        const auto original = storage;
        const auto payload = std::span<const std::byte>(storage).subspan(offset, 257);
        for (std::size_t length = 0; length <= 257; ++length) {
            const auto expected = serial_oracle(payload.first(length));
            expect_hash(payload, static_cast<std::int32_t>(length), expected,
                        "every prefix at every tested byte offset");
        }
        expect_full(payload, serial_oracle(payload), "unaligned complete span");
        expect_error(payload, 258, ResourceHashError::count_exceeds_input,
                     "short input refusal is independent of byte offset");
        check(storage == original, "input and leading/trailing canaries unchanged");
    }

    Bytes first(512, std::byte{0x39});
    Bytes second = first;
    std::fill(second.begin() + 73, second.end(), std::byte{0xFF});
    const auto expected = serial_oracle(std::span<const std::byte>(first).first(73));
    expect_hash(first, 73, expected, "prefix excludes original suffix");
    expect_hash(second, 73, expected, "prefix excludes different suffix");

    // A SIZE_MAX span would violate std::span's valid-range precondition.
    // This representability assertion does not pretend to exercise the
    // full-span overload's unreachable-with-small-storage oversized input.
    check(std::numeric_limits<std::size_t>::max() >
              static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()),
          "SIZE_MAX is outside the native signed-count range");
}

void test_descriptors_and_long_inputs() {
    std::array<std::byte, 88> descriptor{};
    expect_full(descriptor, 0x939323BCD3DB3625ULL, "88 zero bytes anchor");
    descriptor.fill(std::byte{0xFF});
    expect_full(descriptor, 0xE415DF8FFA09E201ULL, "88 FF bytes anchor");
    for (std::size_t i = 0; i < descriptor.size(); ++i) {
        descriptor[i] = std::byte(i);
    }
    expect_full(descriptor, 0x129E5F339292F7E1ULL,
                "ascending synthetic descriptor anchor");
    const auto base = descriptor;
    for (std::size_t i = 0; i < descriptor.size(); ++i) {
        for (unsigned int bit = 0; bit < 8; ++bit) {
            descriptor = base;
            descriptor[i] ^= std::byte(1U << bit);
            expect_full(descriptor, serial_oracle(descriptor),
                        "every bit in an 88-byte synthetic descriptor");
        }
    }
    descriptor = base;
    descriptor[0x30] = std::byte{1};
    expect_hash(descriptor, 88, serial_oracle(descriptor),
                "synthetic descriptor stage byte one");
    descriptor[0x30] = std::byte{0};
    expect_hash(descriptor, 88, serial_oracle(descriptor),
                "synthetic descriptor stage byte zero");

    std::uint64_t random_state = 0xA7E4912F33D0816BULL;
    const auto next = [&random_state]() {
        random_state ^= random_state << 13U;
        random_state ^= random_state >> 7U;
        random_state ^= random_state << 17U;
        return random_state;
    };
    for (std::size_t trial = 0; trial < 512; ++trial) {
        const auto size = static_cast<std::size_t>(next() % 8193U);
        Bytes input(size);
        for (auto& value : input) {
            value = std::byte(next() & 255U);
        }
        const auto original = input;
        expect_full(input, serial_oracle(input), "deterministic random valid span");
        const auto prefix = static_cast<std::size_t>(next() % (size + 1));
        expect_hash(input, static_cast<std::int32_t>(prefix),
                    serial_oracle(std::span<const std::byte>(input).first(prefix)),
                    "deterministic random valid prefix");
        check(input == original, "random input not modified");
    }
    Bytes long_input(1024U * 1024U);
    for (std::size_t i = 0; i < long_input.size(); ++i) {
        long_input[i] = std::byte((i * 17U + (i >> 7U)) & 255U);
    }
    expect_full(long_input, serial_oracle(long_input), "one MiB valid input");
    const auto upper = text_bytes(std::string_view("Ab/C\\d\0X", 8));
    const auto lower = text_bytes(std::string_view("ab/c/d\0x", 8));
    expect_full(upper, serial_oracle(upper), "case slash and NUL remain raw bytes");
    expect_full(lower, serial_oracle(lower), "no resource-name normalization");
    check(serial_oracle(upper) != serial_oracle(lower),
          "normalization-sensitive test vectors are distinguishable");
}

void test_guarded_read_boundaries() {
#if defined(_WIN32)
    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);
    const auto page = static_cast<std::size_t>(system_info.dwPageSize);
    check(page > 8 && page <=
                           static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()),
          "fixture page count fits signed count");
    if (page <= 8 || page >
                         static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        return;
    }
    void* allocation = VirtualAlloc(nullptr, 2 * page,
                                    MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    check(allocation != nullptr, "source-owned guarded allocation");
    if (allocation == nullptr) {
        return;
    }
    // Start a real byte-array lifetime before changing its page protections.
    // These spans describe this array, never fabricated inaccessible ranges.
    auto* bytes = ::new (allocation) std::byte[2 * page];
    std::fill_n(bytes, 2 * page, std::byte{0x5B});
    const auto input = std::span<const std::byte>(bytes, 2 * page);
    const auto expected = serial_oracle(input.first(page));
    DWORD previous = 0;
    const bool second_guarded =
        VirtualProtect(bytes + page, page, PAGE_NOACCESS, &previous) != FALSE;
    check(second_guarded, "guard the suffix of a real allocated byte array");
    if (second_guarded) {
        expect_hash(input, static_cast<std::int32_t>(page), expected,
                    "exact prefix ends before no-access suffix");
        const auto short_input = std::span<const std::byte>(bytes + page - 5, 5);
        expect_error(short_input, 6, ResourceHashError::count_exceeds_input,
                     "short-span error occurs before reading guarded next byte");
        const auto empty_at_guard = std::span<const std::byte>(bytes + page, 0);
        expect_error(empty_at_guard, std::numeric_limits<std::int32_t>::max(),
                     ResourceHashError::count_exceeds_input,
                     "INT32_MAX on real empty span rejects before any read");
    }
    const bool all_guarded =
        VirtualProtect(bytes, 2 * page, PAGE_NOACCESS, &previous) != FALSE;
    check(all_guarded, "guard the complete real array");
    if (all_guarded) {
        expect_hash(input, 0, 0x0CD5E44AFAC432B1ULL,
                    "zero count performs no input read");
        expect_hash(input, -1, 0x0CD5E44AFAC432B1ULL,
                    "negative count performs no input read");
        expect_hash(input, std::numeric_limits<std::int32_t>::min(),
                    0x0CD5E44AFAC432B1ULL,
                    "INT32_MIN performs no input read");
    }
    check(VirtualFree(allocation, 0, MEM_RELEASE) != FALSE,
          "release source-owned guarded storage");
#endif
}

} // namespace

int main() {
    test_fixed_vectors();
    test_all_bytes_and_keys();
    test_counts_prefixes_offsets_and_canaries();
    test_descriptors_and_long_inputs();
    test_guarded_read_boundaries();
    std::cout << "resource_hash: " << checks << " checks, " << failures
              << " failures\n";
    return failures == 0 ? 0 : 1;
}
