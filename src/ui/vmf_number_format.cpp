#include "tdur/ui/vmf_number_format.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <emmintrin.h>
#include <limits>
#include <xmmintrin.h>

namespace tdur::ui {
namespace {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559 &&
              std::numeric_limits<float>::digits == 24);
static_assert(sizeof(int) == 4);

std::size_t append_digits(char* output, std::uint32_t value) noexcept {
    std::array<char, 10> reversed{};
    std::size_t count = 0;
    do {
        reversed[count++] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    } while (value != 0);
    for (std::size_t index = 0; index != count; ++index)
        output[index] = reversed[count - index - 1];
    return count;
}

std::size_t decimal_digits(std::uint32_t value) noexcept {
    std::size_t count = 1;
    while (value >= 10U) {
        value /= 10U;
        ++count;
    }
    return count;
}
} // namespace

std::expected<VmfNumberText, VmfNumberFormatError>
vmf_number_to_string(float value, int precision) noexcept {
    if (precision < 0)
        return std::unexpected(VmfNumberFormatError::negative_precision);

    VmfNumberText result;
    auto* output = result.characters_.data();
    std::size_t length = 0;
    auto magnitude = _mm_set_ss(value);
    const auto zero = _mm_setzero_ps();
    const auto one = _mm_set_ss(1.0f);

    // COMISS zero,value followed by JBE: unordered and signed zero do not
    // emit a sign. DAZ applies to this comparison, as in the original SSE.
    if (_mm_comigt_ss(zero, magnitude)) {
        output[length++] = '-';
        magnitude = _mm_sub_ss(zero, magnitude);
    }

    auto whole = _mm_cvtsi32_ss(zero, _mm_cvttss_si32(magnitude));
    auto fraction = _mm_sub_ss(magnitude, whole);
    const auto epsilon = _mm_set_ss(std::bit_cast<float>(0x3a83126fU));
    if (_mm_comige_ss(_mm_add_ss(fraction, epsilon), one)) {
        whole = _mm_add_ss(whole, one);
        fraction = zero;
    }

    precision = std::min(precision, 4);
    auto scale = one;
    for (int index = 0; index != precision; ++index)
        scale = _mm_mul_ss(scale, _mm_set_ss(10.0f));

    const auto integer = _mm_cvttss_si32(whole);
    const auto rounded_fraction =
        _mm_add_ss(_mm_mul_ss(fraction, scale), _mm_set_ss(0.5f));
    auto numerator = _mm_cvttss_si32(rounded_fraction);

    // _itoa(...,10) semantics, including INT32_MIN, without a signed
    // negation overflow. The native initial '-' remains a separate byte.
    auto integer_magnitude = static_cast<std::uint32_t>(integer);
    if (integer < 0) {
        output[length++] = '-';
        integer_magnitude = 0U - integer_magnitude;
    }
    length += append_digits(output + length, integer_magnitude);

    if (numerator > 0) {
        while (numerator % 10 == 0) {
            numerator /= 10;
            --precision;
        }
        output[length++] = '.';
        const auto digits =
            decimal_digits(static_cast<std::uint32_t>(numerator));
        while (precision > 0 && static_cast<std::size_t>(precision) > digits) {
            output[length++] = '0';
            --precision;
        }
        length += append_digits(output + length,
                                static_cast<std::uint32_t>(numerator));
    }

    // Conservative bound: two signs, ten integer digits, a dot, four
    // padding zeros, ten numerator digits and NUL fit in the owned 32 bytes.
    output[length] = '\0';
    result.length_ = static_cast<std::uint8_t>(length);
    return result;
}

} // namespace tdur::ui
