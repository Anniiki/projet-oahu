#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "tdur/ui/vmf_transform.hpp"
#include <Windows.h>
#include <cstdint>
#include <iostream>
#include <xmmintrin.h>

extern "C" std::uint32_t review_ordered_mul(std::uint32_t,
                                            std::uint32_t) noexcept;
extern "C" std::uint32_t review_ordered_add(std::uint32_t,
                                            std::uint32_t) noexcept;
namespace {
volatile std::uint32_t sink{};
using tdur::ui::VmfTransform;
std::uint32_t ordered_prefix(const VmfTransform& p,
                             const VmfTransform& l) noexcept {
    const auto a0 = review_ordered_mul(p.matrix_bits[3], l.matrix_bits[2]);
    const auto a1 = review_ordered_mul(p.matrix_bits[0], l.matrix_bits[0]);
    sink = review_ordered_add(a0, a1);
    // Closed secondary order at442EA0..442EAF: first p.a*l.c, then p.c*l.d.
    const auto c0 = review_ordered_mul(p.matrix_bits[0], l.matrix_bits[3]);
    const auto c1 = review_ordered_mul(p.matrix_bits[3], l.matrix_bits[1]);
    return review_ordered_add(c0, c1);
}
unsigned invoke(bool reference, const VmfTransform& p, const VmfTransform& l) {
    __try {
        if (reference)
            sink = ordered_prefix(p, l);
        else
            sink = tdur::ui::compose_vmf_transform(p, l).matrix_bits[3];
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
}
} // namespace
int main() {
    const unsigned saved = _mm_getcsr();
    const unsigned base = (saved | 0x1f80U) & ~0xe07fU;
    // Both exceptional products are after an exact safe a component.
    // Native order: c0 overflows before c1=0*infinity raises invalid.
    VmfTransform p, l;
    p.matrix_bits = {0x7f7fffffU, 0x3f800000U, 0, 0, 0, 0};
    l.matrix_bits = {0, 0x7f800000U, 0, 0x40000000U, 0, 0};
    const unsigned control = base & ~(0x80U | 0x400U);
    _mm_setcsr(control);
    const auto expected = invoke(true, p, l);
    _mm_setcsr(control);
    const auto actual = invoke(false, p, l);
    _mm_setcsr(saved);
    std::cout << "pointer_bits=" << sizeof(void*) * 8 << " control=" << std::hex
              << control << " expected_exception=" << expected
              << " actual_exception=" << actual
              << " overflow=" << unsigned(EXCEPTION_FLT_OVERFLOW)
              << " invalid=" << unsigned(EXCEPTION_FLT_INVALID_OPERATION)
              << '\n';
    return expected == unsigned(EXCEPTION_FLT_OVERFLOW) && actual == expected
               ? 0
               : 1;
}
