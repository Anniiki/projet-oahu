#include "vmf_transform_ops.hpp"

#include <bit>
#include <emmintrin.h>

namespace tdur::ui::detail {
namespace {
#if !defined(_MSC_VER) || !defined(_M_IX86)
__m128 scalar(std::uint32_t bits) noexcept {
    return _mm_castsi128_ps(
        _mm_cvtsi32_si128(std::bit_cast<std::int32_t>(bits)));
}

std::uint32_t bits(__m128 value) noexcept {
    return static_cast<std::uint32_t>(
        _mm_cvtsi128_si32(_mm_castps_si128(value)));
}
#endif
} // namespace

std::uint32_t multiply_vmf_transform_bits(std::uint32_t lhs,
                                          std::uint32_t rhs) noexcept {
#if defined(_MSC_VER) && defined(_M_IX86)
    // Win32 commutes some intrinsic operands even under /fp:strict, changing
    // which NaN payload survives. The actual arithmetic remains scalar SSE.
    std::uint32_t result;
    __asm {
        movd xmm0, lhs
        movd xmm1, rhs
        mulss xmm0, xmm1
        movd result, xmm0
    }
    return result;
#else
    return bits(_mm_mul_ss(scalar(lhs), scalar(rhs)));
#endif
}

std::uint32_t add_vmf_transform_bits(std::uint32_t lhs,
                                     std::uint32_t rhs) noexcept {
#if defined(_MSC_VER) && defined(_M_IX86)
    std::uint32_t result;
    __asm {
        movd xmm0, lhs
        movd xmm1, rhs
        addss xmm0, xmm1
        movd result, xmm0
    }
    return result;
#else
    return bits(_mm_add_ss(scalar(lhs), scalar(rhs)));
#endif
}

} // namespace tdur::ui::detail
