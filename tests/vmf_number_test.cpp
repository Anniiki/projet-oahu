#include "tdur/ui/vmf_number.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cfenv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <xmmintrin.h>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#pragma fenv_access(on)

// Only ordinary C++ new/new[] are observed. Direct malloc/locale accesses are
// not instrumented; absence of those also requires source review.
namespace number_allocation_fixture {
thread_local bool enabled = false;
thread_local std::size_t calls = 0;

void* allocate(std::size_t size) {
    if (enabled) {
        ++calls;
    }
    if (auto* result = std::malloc(size == 0 ? 1 : size)) {
        return result;
    }
    throw std::bad_alloc();
}
} // namespace number_allocation_fixture

void* operator new(std::size_t size) {
    return number_allocation_fixture::allocate(size);
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept {
    std::free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept {
    std::free(pointer);
}

namespace {
using tdur::ui::vmf_number_from_string;
std::size_t checks = 0;
std::size_t failures = 0;
bool fp_restore_failed = false;

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
static_assert(std::numeric_limits<float>::digits == 24);
static_assert(noexcept(vmf_number_from_string(std::string_view{})));
static_assert(std::is_same_v<decltype(vmf_number_from_string({})), float>);

void check(bool passed, std::string_view label, std::size_t index = 0) {
    ++checks;
    if (!passed) {
        ++failures;
        if (failures <= 32) {
            std::cerr << "FAIL[" << index << "]: " << label << '\n';
        }
    }
}

class AllocationCount {
  public:
    AllocationCount() noexcept
        : previous_enabled_(number_allocation_fixture::enabled),
          previous_calls_(number_allocation_fixture::calls) {
        number_allocation_fixture::calls = 0;
        number_allocation_fixture::enabled = true;
    }
    ~AllocationCount() noexcept {
        number_allocation_fixture::enabled = previous_enabled_;
        number_allocation_fixture::calls = previous_calls_;
    }
    AllocationCount(const AllocationCount&) = delete;
    AllocationCount& operator=(const AllocationCount&) = delete;
    [[nodiscard]] std::size_t calls() const noexcept {
        return number_allocation_fixture::calls;
    }

  private:
    bool previous_enabled_;
    std::size_t previous_calls_;
};

// All arithmetic oracle tables assume binary32 RNE with gradual underflow.
// Scoped alternatives exercise the caller's rounding/FTZ settings. Restore
// the C FP environment AND raw MXCSR (including its prior status flags).
class FpScope {
  public:
    explicit FpScope(unsigned rounding = 0, unsigned extra_controls = 0)
        : saved_csr_(_mm_getcsr()) {
        if (std::fegetenv(&saved_env_) != 0) {
            throw std::runtime_error("cannot save FP environment");
        }
        auto next = (saved_csr_ | 0x1f80U) & ~(0x6000U | 0x8040U | 0x3fU);
        _mm_setcsr(next | rounding | extra_controls);
    }
    ~FpScope() noexcept {
        if (std::fesetenv(&saved_env_) != 0) {
            fp_restore_failed = true;
        }
        _mm_setcsr(saved_csr_);
    }
    FpScope(const FpScope&) = delete;
    FpScope& operator=(const FpScope&) = delete;

  private:
    std::fenv_t saved_env_{};
    unsigned saved_csr_;
};

float observed(std::string_view input) {
    const auto before_csr = _mm_getcsr();
    const auto before_round = std::fegetround();
    const auto before_errno = errno;
    std::size_t allocation_calls = 0;
    float result;
    {
        AllocationCount allocation_scope;
        result = vmf_number_from_string(input);
        allocation_calls = allocation_scope.calls();
    }
    const auto after_csr = _mm_getcsr();
    const auto after_round = std::fegetround();
    const auto after_errno = errno;
    check(allocation_calls == 0, "no ordinary new allocation");
    // Arithmetic MAY set IEEE status bits0..5, including invalid/overflow.
    check((before_csr & ~0x3fU) == (after_csr & ~0x3fU),
          "MXCSR controls unchanged");
    check(before_round == after_round, "caller FP rounding remains selected");
    check(before_errno == after_errno, "converter does not alter errno");
    return result;
}

void expect(std::string_view input, std::uint32_t expected_bits,
            bool expected_nan = false, std::size_t index = 0) {
    const auto actual_bits = std::bit_cast<std::uint32_t>(observed(input));
    const bool passed = expected_nan ? (actual_bits & 0x7fffffffU) > 0x7f800000U
                                     : actual_bits == expected_bits;
    check(passed,
          expected_nan ? "NaN class, payload/sign not specified"
                       : "exact binary32 oracle",
          index);
    if (!passed && failures <= 32) {
        std::cerr << "  expected=0x" << std::hex << expected_bits
                  << " actual=0x" << actual_bits << std::dec << '\n';
    }
}

struct Vector {
    std::string_view input;
    std::uint32_t bits;
    bool nan;
};

// Core127 vectors frozen before src/ui/vmf_number.cpp existed.
// Independent exact-Fraction IEEE754 rounding + four-lane dependency trace,
// not strtof and not a loop copied from the C++ implementation. The private
// generation/provenance data are vmf-number-tests/core-oracle-frozen.json.
// SSE evidence10:
// ev_15f0cce6a2fb910768bae6f1e7a9c76885f3b0d79766fde3ac844098739fc097.
constexpr std::array<Vector, 127> vectors{{
    {std::string_view{"", 0}, 0x00000000U, false},
    {std::string_view{"+", 1}, 0x00000000U, false},
    {std::string_view{"-", 1}, 0x80000000U, false},
    {std::string_view{"0", 1}, 0x00000000U, false},
    {std::string_view{"-0", 2}, 0x80000000U, false},
    {std::string_view{"+0", 2}, 0x00000000U, false},
    {std::string_view{"-.", 2}, 0x80000000U, false},
    {std::string_view{".", 1}, 0x00000000U, false},
    {std::string_view{"..", 2}, 0xbe4ccccdU, false},
    {std::string_view{"...", 3}, 0xbe6147aeU, false},
    {std::string_view{"1", 1}, 0x3f800000U, false},
    {std::string_view{"-1", 2}, 0xbf800000U, false},
    {std::string_view{"12", 2}, 0x41400000U, false},
    {std::string_view{"123", 3}, 0x42f60000U, false},
    {std::string_view{"1234", 4}, 0x449a4000U, false},
    {std::string_view{"12345", 5}, 0x4640e400U, false},
    {std::string_view{"1234567", 7}, 0x4996b438U, false},
    {std::string_view{"12345678", 8}, 0x4b3c614eU, false},
    {std::string_view{"123456789", 9}, 0x4ceb79a2U, false},
    {std::string_view{"0.1", 3}, 0x3dcccccdU, false},
    {std::string_view{"-0.1", 4}, 0xbdcccccdU, false},
    {std::string_view{"0.2", 3}, 0x3e4ccccdU, false},
    {std::string_view{"0.3", 3}, 0x3e99999aU, false},
    {std::string_view{"1.5", 3}, 0x3fc00000U, false},
    {std::string_view{".25", 3}, 0x3e800000U, false},
    {std::string_view{"-.25", 4}, 0xbe800000U, false},
    {std::string_view{"12.375", 6}, 0x41460000U, false},
    {std::string_view{"1..2", 4}, 0x3f51eb85U, false},
    {std::string_view{"..1", 3}, 0xbe428f5cU, false},
    {std::string_view{".1.2", 4}, 0x3da7ef9dU, false},
    {std::string_view{"1.2.3", 5}, 0x3f976c8bU, false},
    {std::string_view{"1...", 4}, 0x3f47ae14U, false},
    {std::string_view{"-..", 3}, 0x3e4ccccdU, false},
    {std::string_view{"--1", 3}, 0x80000000U, false},
    {std::string_view{"++1", 3}, 0x00000000U, false},
    {std::string_view{"+-1", 3}, 0x00000000U, false},
    {std::string_view{" 12", 3}, 0x00000000U, false},
    {std::string_view{"\011" "12", 3}, 0x00000000U, false},
    {std::string_view{"- 12", 4}, 0x80000000U, false},
    {std::string_view{"12 3", 4}, 0x41400000U, false},
    {std::string_view{"1,5", 3}, 0x3f800000U, false},
    {std::string_view{"nan", 3}, 0x00000000U, false},
    {std::string_view{"NaN", 3}, 0x00000000U, false},
    {std::string_view{"INF", 3}, 0x00000000U, false},
    {std::string_view{"inf", 3}, 0x00000000U, false},
    {std::string_view{"1e2", 3}, 0x00000000U, false},
    {std::string_view{"-1e2", 4}, 0x00000000U, false},
    {std::string_view{"12junk e", 8}, 0x00000000U, false},
    {std::string_view{"1E2", 3}, 0x3f800000U, false},
    {std::string_view{"1E2e", 4}, 0x00000000U, false},
    {std::string_view{"E12", 3}, 0x00000000U, false},
    {std::string_view{"e", 1}, 0x00000000U, false},
    {std::string_view{"-e", 2}, 0x00000000U, false},
    {std::string_view{"12\000e", 4}, 0x41400000U, false},
    {std::string_view{"-0\000e", 4}, 0x80000000U, false},
    {std::string_view{"\000e", 2}, 0x00000000U, false},
    {std::string_view{"123junk", 7}, 0x42f60000U, false},
    {std::string_view{"123junque", 9}, 0x00000000U, false},
    {std::string_view{"0x10", 4}, 0x00000000U, false},
    {std::string_view{"0x1p3", 5}, 0x00000000U, false},
    {std::string_view{"\377" "12", 3}, 0x00000000U, false},
    {std::string_view{"12\200" "1", 4}, 0x41400000U, false},
    {std::string_view{"1.2\377" "9", 5}, 0x3f99999aU, false},
    {std::string_view{"\377e", 2}, 0x00000000U, false},
    {std::string_view{"12\000\377e", 5}, 0x41400000U, false},
    {std::string_view{"16777215", 8}, 0x4b7fffffU, false},
    {std::string_view{"16777216", 8}, 0x4b800000U, false},
    {std::string_view{"16777217", 8}, 0x4b800000U, false},
    {std::string_view{"16777218", 8}, 0x4b800001U, false},
    {std::string_view{"16777219", 8}, 0x4b800002U, false},
    {std::string_view{"16777221", 8}, 0x4b800002U, false},
    {std::string_view{"-16777217", 9}, 0xcb800000U, false},
    {std::string_view{"-16777219", 9}, 0xcb800002U, false},
    {std::string_view{"33554431", 8}, 0x4c000000U, false},
    {std::string_view{"33554433", 8}, 0x4c000000U, false},
    {std::string_view{"99999999", 8}, 0x4cbebc20U, false},
    {std::string_view{"100000001", 9}, 0x4cbebc20U, false},
    {std::string_view{"100000007", 9}, 0x4cbebc21U, false},
    {std::string_view{"100000009", 9}, 0x4cbebc21U, false},
    {std::string_view{"4294967295", 10}, 0x4f800000U, false},
    {std::string_view{"2147483647", 10}, 0x4f000000U, false},
    {std::string_view{"999999999999", 12}, 0x5368d4a5U, false},
    {std::string_view{"12345678901234567890", 20}, 0x5f2b54aaU, false},
    {std::string_view{"100000000000000000001", 21}, 0x60ad78ecU, false},
    {std::string_view{"18446744073709551615", 20}, 0x5f800000U, false},
    {std::string_view{"0.123456789", 11}, 0x3dfcd6eaU, false},
    {std::string_view{"1.234567890123456789", 20}, 0x3f9e0652U, false},
    {std::string_view{"3.14159265358979323846", 22}, 0x40490fdbU, false},
    {std::string_view{"2.71828182845904523536", 22}, 0x402df854U, false},
    {std::string_view{"0.99999999999999999999", 22}, 0x3f800000U, false},
    {std::string_view{"123456.7890123", 14}, 0x47f12065U, false},
    {std::string_view{"387583431047", 12}, 0x52b47b8bU, false},
    {std::string_view{"508601127788", 12}, 0x52ecd5fbU, false},
    {std::string_view{"634737109949", 12}, 0x5313c947U, false},
    {std::string_view{"618141572505", 12}, 0x530fec1bU, false},
    {std::string_view{"340282346638528859811704183484516925440", 39},
     0x7f800000U, false},
    {std::string_view{"-340282346638528859811704183484516925440", 40},
     0xff800000U, false},
    {std::string_view{"-999999999999999999999999999999999999999", 40},
     0xff800000U, false},
    {std::string_view{"10000000", 8}, 0x4b189680U, false},
    {std::string_view{"100000000", 9}, 0x4cbebc20U, false},
    {std::string_view{"10000000000", 11}, 0x501502f9U, false},
    {std::string_view{"1000000000000000", 16}, 0x58635fa9U, false},
    {std::string_view{"100000000000000000000", 21}, 0x60ad78ecU, false},
    {std::string_view{"1000000000000000000000000000000", 31}, 0x7149f2caU,
     false},
    {std::string_view{"10000000000000000000000000000000000000", 38},
     0x7cf0bdc3U, false},
    {std::string_view{"100000000000000000000000000000000000000", 39},
     0x7e96769aU, false},
    {std::string_view{"1000000000000000000000000000000000000000", 40},
     0x7f800000U, false},
    {std::string_view{"10000000000000000000000000000000000000000", 41},
     0x7fc00000U, true},
    {std::string_view{"100000000000000000000000000000000000000000", 42},
     0x7fc00000U, true},
    {std::string_view{"00000000000000000000000000000000000000", 38},
     0x00000000U, false},
    {std::string_view{"99999999999999999999999999999999999999", 38},
     0x7e96769aU, false},
    {std::string_view{"000000000000000000000000000000000000000", 39},
     0x00000000U, false},
    {std::string_view{"999999999999999999999999999999999999999", 39},
     0x7f800000U, false},
    {std::string_view{"0000000000000000000000000000000000000000", 40},
     0x7fc00000U, true},
    {std::string_view{"9999999999999999999999999999999999999999", 40},
     0x7f800000U, false},
    {std::string_view{"00000000000000000000000000000000000000000", 41},
     0x7fc00000U, true},
    {std::string_view{"99999999999999999999999999999999999999999", 41},
     0x7f800000U, false},
    {std::string_view{"0.000000000000000000000000000000000001", 38},
     0x03aa2424U, false},
    {std::string_view{"-0.000000000000000000000000000000000001", 39},
     0x83aa2424U, false},
    {std::string_view{"0.0000000000000000000000000000000000001", 39},
     0x02081ceaU, false},
    {std::string_view{"-0.0000000000000000000000000000000000001", 40},
     0x82081ceaU, false},
    {std::string_view{"0.00000000000000000000000000000000000001", 40},
     0x006ce3eeU, false},
    {std::string_view{"-0.00000000000000000000000000000000000001", 41},
     0x806ce3eeU, false},
    {std::string_view{"0.000000000000000000000000000000000000001", 41},
     0x00000000U, false},
    {std::string_view{"-0.000000000000000000000000000000000000001", 42},
     0x80000000U, false},
    {std::string_view{"0.0000000000000000000000000000000000000001", 42},
     0x00000000U, false},
    {std::string_view{"-0.0000000000000000000000000000000000000001", 43},
     0x80000000U, false},
}};

struct DirectedVector {
    std::string_view input;
    std::array<std::uint32_t, 4> bits; // nearest,down,up,towardzero
    std::array<bool, 4> nan;
};
constexpr std::array<DirectedVector, 7> directed{{
    {std::string_view{"16777217", 8},
     {{0x4b800000U, 0x4b800000U, 0x4b800001U, 0x4b800000U}},
     {{false, false, false, false}}},
    {std::string_view{"-16777217", 9},
     {{0xcb800000U, 0xcb800000U, 0xcb800001U, 0xcb800000U}},
     {{false, false, false, false}}},
    {std::string_view{"0.1", 3},
     {{0x3dcccccdU, 0x3dccccccU, 0x3dcccccdU, 0x3dccccccU}},
     {{false, false, false, false}}},
    {std::string_view{"-0.1", 4},
     {{0xbdcccccdU, 0xbdccccccU, 0xbdcccccdU, 0xbdccccccU}},
     {{false, false, false, false}}},
    {std::string_view{"1..2", 4},
     {{0x3f51eb85U, 0x3f51eb85U, 0x3f51eb86U, 0x3f51eb85U}},
     {{false, false, false, false}}},
    {std::string_view{"0000000000000000000000000000000000000000", 40},
     {{0x7fc00000U, 0x00000000U, 0x7fc00000U, 0x00000000U}},
     {{true, false, true, false}}},
    {std::string_view{"1000000000000000000000000000000000000000", 40},
     {{0x7f800000U, 0x7f7fffffU, 0x7f800000U, 0x7f7fffffU}},
     {{false, false, false, false}}},
}};

void test_fixed_vectors() {
    FpScope environment;
    errno = EDOM;
    for (std::size_t i = 0; i < vectors.size(); ++i) {
        expect(vectors[i].input, vectors[i].bits, vectors[i].nan, i);
    }
    expect({}, 0); // Default view: null data,zero length.
}

void test_each_byte_and_views() {
    FpScope environment;
    for (unsigned value = 0; value < 256; ++value) {
        const bool digit = value >= '0' && value <= '9';
        const auto digit_bits =
            digit
                ? std::bit_cast<std::uint32_t>(static_cast<float>(value - '0'))
                : 0U;
        const char character =
            static_cast<char>(static_cast<unsigned char>(value));
        const std::array single{character};
        expect(std::string_view(single.data(), single.size()),
               value == '-' ? 0x80000000U : digit_bits, false, value);
        const std::array suffix{'1', '2', character, '3'};
        const auto suffix_bits =
            value == 'e'   ? 0U
            : value == '.' ? 0x4144cccdU
            : digit        ? std::bit_cast<std::uint32_t>(
                          static_cast<float>(1203U + 10U * (value - '0')))
                    : 0x41400000U;
        expect(std::string_view(suffix.data(), suffix.size()), suffix_bits,
               false, value);
        const std::array negative{'-', character};
        expect(std::string_view(negative.data(), negative.size()),
               value == 'e' ? 0U : 0x80000000U | digit_bits, false, value);
    }
    const std::array surrounded{'e', '1', '2', 'e', '\0'};
    expect(std::string_view(surrounded.data() + 1, 2), 0x41400000U);
    const std::array sign_subview{'e', '-', '1', 'e'};
    expect(std::string_view(sign_subview.data() + 1, 2), 0xbf800000U);
    const std::array char_after_nul{'1', '2', '\0', 'e', '\xff'};
    expect(std::string_view(char_after_nul.data(), char_after_nul.size()),
           0x41400000U);
}

void test_rounding_and_flush() {
    constexpr std::array<unsigned, 4> rounding{0, 0x2000U, 0x4000U, 0x6000U};
    for (std::size_t mode = 0; mode < rounding.size(); ++mode) {
        const auto saved = _mm_getcsr();
        {
            FpScope environment(rounding[mode]);
            for (std::size_t i = 0; i < directed.size(); ++i) {
                expect(directed[i].input, directed[i].bits[mode],
                       directed[i].nan[mode], i);
            }
            expect("-e",
                   0); // Early return precedes sign/FP work in every mode.
        }
        check(_mm_getcsr() == saved, "FP fixture restored full prior MXCSR");
    }
    const std::string tiny = std::string("0.") + std::string(37, '0') + "1";
    const auto negative = std::string("-") + tiny;
    {
        FpScope environment;
        expect(tiny, 0x006ce3eeU);
        expect(negative, 0x806ce3eeU);
    }
    {
        FpScope environment(0, 0x8000U);
        expect(tiny, 0);
        expect(negative, 0x80000000U);
    }
}

void test_long_inputs_and_early_exit() {
    FpScope environment;
    const std::string zeros(4096, '0');
    expect(zeros, 0x7fc00000U, true);
    const auto fraction = std::string("0.") + zeros + "1";
    expect(fraction, 0);
    auto early = zeros;
    early.back() = 'e';
    // Pre-existing status must survive an integer-only early return. In
    // particular, a converter must not first overflow while parsing zeros.
    _mm_setcsr((_mm_getcsr() & ~0x3fU) | 0x20U);
    const auto before = _mm_getcsr();
    expect(early, 0);
    check(_mm_getcsr() == before, "lowercase e exits before any FP arithmetic");
    const std::array exact{'1', '2'};
    expect(std::string_view(exact.data(), exact.size()), 0x41400000U);
    check(_mm_getcsr() == before,
          "exact arithmetic preserves pre-existing FP flags");
    const auto content = early;
    expect(early, 0);
    check(early == content, "borrowed caller bytes unchanged");
}

#if defined(_WIN32)
class GuardedPage {
  public:
    GuardedPage() {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        size_ = info.dwPageSize;
        base_ = static_cast<char*>(VirtualAlloc(
            nullptr, size_ * 3, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS));
        if (!base_) {
            throw std::runtime_error("guard page allocation failed");
        }
        DWORD previous = 0;
        if (!VirtualProtect(base_ + size_, size_, PAGE_READWRITE, &previous)) {
            VirtualFree(base_, 0, MEM_RELEASE);
            base_ = nullptr;
            throw std::runtime_error("guard page protection failed");
        }
    }
    ~GuardedPage() noexcept {
        if (base_) {
            VirtualFree(base_, 0, MEM_RELEASE);
        }
    }
    GuardedPage(const GuardedPage&) = delete;
    GuardedPage& operator=(const GuardedPage&) = delete;
    [[nodiscard]] char* begin() const noexcept { return base_ + size_; }
    [[nodiscard]] char* end() const noexcept { return base_ + size_ * 2; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }

  private:
    char* base_ = nullptr;
    std::size_t size_ = 0;
};

void test_guarded_view_bounds() {
    FpScope environment;
    GuardedPage page;
    constexpr std::array<Vector, 8> edge{{{{"123.5", 5}, 0x42f70000U, false},
                                          {{"-", 1}, 0x80000000U, false},
                                          {{"+", 1}, 0U, false},
                                          {{"1..2", 4}, 0x3f51eb85U, false},
                                          {{"12\0e", 4}, 0x41400000U, false},
                                          {{"-1", 2}, 0xbf800000U, false},
                                          {{"e", 1}, 0U, false},
                                          {{"\xff", 1}, 0U, false}}};
    for (const auto& value : edge) {
        auto* location = page.end() - value.input.size();
        std::memcpy(location, value.input.data(), value.input.size());
        expect(std::string_view(location, value.input.size()), value.bits,
               value.nan);
        check(std::memcmp(location, value.input.data(), value.input.size()) ==
                  0,
              "view at guard end unchanged");
        std::memcpy(page.begin(), value.input.data(), value.input.size());
        expect(std::string_view(page.begin(), value.input.size()), value.bits,
               value.nan);
    }
    // Empty view exactly one-past the accessible page: dereference would fault.
    expect(std::string_view(page.end(), 0), 0);
    std::memset(page.begin(), '0', page.size());
    expect(std::string_view(page.begin(), page.size()), 0x7fc00000U, true);
    page.end()[-1] = 'e';
    expect(std::string_view(page.begin(), page.size()), 0);
}
#endif
} // namespace

int main() {
    const auto entry_csr = _mm_getcsr();
    const auto entry_errno = errno;
    const std::array tests{test_fixed_vectors, test_each_byte_and_views,
                           test_rounding_and_flush,
                           test_long_inputs_and_early_exit
#if defined(_WIN32)
                           ,
                           test_guarded_view_bounds
#endif
    };
    for (const auto test : tests) {
        try {
            test();
        } catch (const std::exception& error) {
            check(false, error.what());
        }
    }
    check(!number_allocation_fixture::enabled, "allocation observer disarmed");
    check(!fp_restore_failed && _mm_getcsr() == entry_csr,
          "all scoped FP state restored");
    errno = entry_errno;
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
