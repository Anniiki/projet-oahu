#include "tdur/ui/vmf_number.hpp"

#include <limits>
#include <xmmintrin.h>

namespace tdur::ui {
namespace {
static_assert(std::numeric_limits<float>::is_iec559 &&
              std::numeric_limits<float>::digits == 24 &&
              std::numeric_limits<float>::max_exponent == 128);

bool decimal_digit(char value) noexcept { return value >= '0' && value <= '9'; }
} // namespace

float vmf_number_from_string(std::string_view text) noexcept {
    if (const auto terminator = text.find('\0');
        terminator != std::string_view::npos)
        text = text.substr(0, terminator);
    if (text.find('e') != std::string_view::npos)
        return 0.0f;

    float sign = 1.0f;
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
        if (text.front() == '-')
            sign = -1.0f;
        text.remove_prefix(1);
    }

    std::size_t integer_end = 0;
    while (integer_end < text.size() && decimal_digit(text[integer_end]))
        ++integer_end;
    auto numeric_end = integer_end;
    while (numeric_end < text.size() &&
           (decimal_digit(text[numeric_end]) || text[numeric_end] == '.'))
        ++numeric_end;

    // Each intermediate is binary32. The target uses MULSS/ADDSS/DIVSS;
    // do not contract these operations or replace them by strtof/pow.
    float integer = 0.0f;
    float weight = 1.0f;
    for (auto i = integer_end; i != 0; --i) {
        const float digit = static_cast<float>(text[i - 1] - '0');
        const float contribution = digit * weight;
        integer = contribution + integer;
        weight = weight * 10.0f;
    }

    float fraction = 0.0f;
    float divisor = 10.0f;
    // Only the first dot is skipped. Subsequent dots contribute -2, as in
    // the observed byte subtraction; this is intentionally not a grammar
    // check. Avoid forming integer_end+1 when there is no fractional run.
    if (integer_end < numeric_end) {
        for (auto i = integer_end + 1; i < numeric_end; ++i) {
            const float digit = static_cast<float>(text[i] - '0');
            const float contribution = digit / divisor;
            fraction = contribution + fraction;
            divisor = divisor * 10.0f;
        }
    }
    // The Win32 ABI returns float through x87. Explicit SSE keeps the final
    // arithmetic under MXCSR too; merely storing a C++ float can otherwise
    // make MSVC use x87's independently configured rounding mode here.
    const auto magnitude =
        _mm_add_ss(_mm_set_ss(fraction), _mm_set_ss(integer));
    return _mm_cvtss_f32(_mm_mul_ss(magnitude, _mm_set_ss(sign)));
}

} // namespace tdur::ui
