#include "tdur/ui/vmf_number_format.hpp"

#include "fixtures/vmf_number_format_vectors.hpp"

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
#include <string_view>
#include <type_traits>
#include <utility>
#include <xmmintrin.h>

#pragma fenv_access(on)

namespace format_allocation_fixture {
thread_local bool enabled = false;
thread_local std::size_t calls = 0;
} // namespace format_allocation_fixture

void* operator new(std::size_t size) {
    if (format_allocation_fixture::enabled)
        ++format_allocation_fixture::calls;
    if (auto* result = std::malloc(size == 0 ? 1 : size))
        return result;
    throw std::bad_alloc();
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
using tdur::ui::vmf_number_to_string;
using tdur::ui::VmfNumberFormatError;
using tdur::ui::VmfNumberText;
std::size_t checks = 0;
std::size_t failures = 0;
bool restore_failed = false;

template <class T>
concept RvalueText = requires(T value) { std::move(value).text(); };
template <class T>
concept RvalueCstr = requires(T value) { std::move(value).c_str(); };

static_assert(!RvalueText<VmfNumberText> && !RvalueCstr<VmfNumberText>);
static_assert(std::is_trivially_copyable_v<VmfNumberText>);
static_assert(noexcept(vmf_number_to_string(0.0f)));

void check(bool passed, std::string_view label, std::size_t index = 0) {
    ++checks;
    if (!passed) {
        ++failures;
        if (failures <= 20)
            std::cerr << "FAIL[" << index << "]: " << label << '\n';
    }
}

class FpScope {
  public:
    explicit FpScope(unsigned mode, unsigned extra = 0)
        : saved_csr_(_mm_getcsr()) {
        if (std::fegetenv(&saved_environment_) != 0)
            throw std::runtime_error("cannot save FP environment");
        // Only MXCSR changes. The x87 rounding control remains independent.
        _mm_setcsr(((saved_csr_ | 0x1f80U) & ~0xe07fU) | mode | extra);
    }
    ~FpScope() noexcept {
        if (std::fesetenv(&saved_environment_) != 0)
            restore_failed = true;
        _mm_setcsr(saved_csr_);
    }
    FpScope(const FpScope&) = delete;
    FpScope& operator=(const FpScope&) = delete;

  private:
    std::fenv_t saved_environment_{};
    unsigned saved_csr_;
};

auto observed(std::uint32_t bits, int precision, std::size_t index) {
    const auto before_csr = _mm_getcsr();
    const auto before_rounding = std::fegetround();
    const auto before_errno = errno;
    format_allocation_fixture::calls = 0;
    format_allocation_fixture::enabled = true;
    auto result = vmf_number_to_string(std::bit_cast<float>(bits), precision);
    format_allocation_fixture::enabled = false;
    const auto allocation_calls = format_allocation_fixture::calls;
    const auto after_errno = errno;
    const auto after_csr = _mm_getcsr();
    const auto after_rounding = std::fegetround();
    check(allocation_calls == 0, "no ordinary C++ allocation", index);
    check(before_errno == after_errno, "errno unchanged", index);
    check(((before_csr ^ after_csr) & ~0x3fU) == 0, "MXCSR controls unchanged",
          index);
    check(before_rounding == after_rounding, "x87 rounding unchanged", index);
    if (result) {
        check(result->text().size() < 32, "bounded length", index);
        check(result->c_str()[result->text().size()] == '\0', "NUL termination",
              index);
        check(std::strlen(result->c_str()) == result->text().size(),
              "no embedded NUL", index);
    }
    return result;
}

void frozen_vectors() {
    constexpr std::array<unsigned, 4> modes{0, 0x2000U, 0x4000U, 0x6000U};
    for (std::size_t mode = 0; mode != modes.size(); ++mode) {
        const FpScope environment(modes[mode]);
        for (std::size_t index = 0; index != vmf_format_fixture::vectors.size();
             ++index) {
            const auto& vector = vmf_format_fixture::vectors[index];
            errno = 127;
            const auto result = observed(vector.bits, vector.precision, index);
            check(result.has_value(), "valid precision accepted", index);
            if (result) {
                const auto matches = result->text() == vector.expected[mode];
                check(matches, "frozen exact-rational string", index);
                if (!matches && failures <= 20)
                    std::cerr << " mode=" << mode << " bits=" << std::hex
                              << vector.bits << std::dec
                              << " precision=" << vector.precision
                              << " expected=" << vector.expected[mode]
                              << " actual=" << result->text() << '\n';
            }
        }
    }
}

void guards_and_ownership() {
    const FpScope environment(0);
    for (const auto precision :
         {-1, -4, -12, std::numeric_limits<int>::min()}) {
        for (const auto bits : {0U, 0x7f800000U, 0x7f800001U, 0xff800000U}) {
            const auto before = _mm_getcsr();
            const auto result = observed(bits, precision, 0);
            check(!result && result.error() ==
                                 VmfNumberFormatError::negative_precision,
                  "negative precision explicitly refused");
            check(before == _mm_getcsr(), "guard performs no FP arithmetic");
        }
    }
    auto first = vmf_number_to_string(-12.375f);
    check(first.has_value(), "first owning value");
    if (!first)
        return;
    auto copy = *first;
    auto moved = std::move(copy);
    const auto another = vmf_number_to_string(999.0f);
    check(another && another->text() == "999", "successive independent call");
    first = vmf_number_to_string(1.5f);
    check(first && first->text() == "1.5", "owning assignment");
    check(moved.text() == "-12.375", "old copied and moved text preserved");
    check(first && moved.c_str() != first->c_str(),
          "owned buffers have distinct addresses");
    const auto default_precision = vmf_number_to_string(0.25f);
    check(default_precision && default_precision->text() == "0.25",
          "native caller default precision12");
}

void denormal_controls() {
    for (const auto controls : {0U, 0x8000U, 0x40U, 0x8040U}) {
        const FpScope environment(0, controls);
        const auto positive = observed(1, 12, controls);
        const auto negative = observed(0x80000001U, 12, controls);
        check(positive && positive->text() == "0", "positive denormal");
        const std::string_view expected = controls & 0x40U ? "0" : "-0";
        check(negative && negative->text() == expected,
              "DAZ controls sign comparison, FTZ retains emitted minus");
    }
}
} // namespace

int main() {
    const auto original_csr = _mm_getcsr();
    const auto original_rounding = std::fegetround();
    frozen_vectors();
    guards_and_ownership();
    denormal_controls();
    check(!restore_failed, "FP restoration succeeded");
    check(original_csr == _mm_getcsr(), "original MXCSR restored");
    check(original_rounding == std::fegetround(), "original x87 restored");
    std::cout << "{\"scope\":\"vmf-number-format\",\"bits\":"
              << sizeof(void*) * 8
              << ",\"vectors\":" << vmf_format_fixture::vectors.size()
              << ",\"modes\":4,\"checks\":" << checks
              << ",\"failures\":" << failures << ",\"game_executed\":false}\n";
    return failures == 0 ? 0 : 1;
}
