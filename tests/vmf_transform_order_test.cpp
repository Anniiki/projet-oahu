#include "tdur/ui/vmf_transform.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <intrin.h>
#include <iomanip>
#include <iostream>
#include <xmmintrin.h>

extern "C" std::uint32_t review_ordered_mul(std::uint32_t lhs,
                                            std::uint32_t rhs) noexcept;
extern "C" std::uint32_t review_ordered_add(std::uint32_t lhs,
                                            std::uint32_t rhs) noexcept;

namespace {
using tdur::ui::VmfTransform;
// Twenty source-owned ordered operations, read from the closed 442E80 graph.
// Registers0..5=parent,6..11=local,12/13=temp,14..19=matrix output.
struct Op {
    bool multiply;
    std::uint8_t dst, lhs, rhs;
};
constexpr std::array<Op, 20> program{
    {{true, 12, 3, 8},    {true, 13, 0, 6},  {false, 14, 12, 13},
     {true, 12, 0, 9},    {true, 13, 3, 7},  {false, 17, 12, 13},
     {true, 12, 0, 10},   {true, 13, 3, 11}, {false, 12, 12, 13},
     {false, 18, 12, 4},  {true, 12, 1, 8},  {true, 13, 2, 6},
     {false, 16, 12, 13}, {true, 12, 2, 9},  {true, 13, 1, 7},
     {false, 15, 12, 13}, {true, 12, 1, 11}, {true, 13, 2, 10},
     {false, 12, 12, 13}, {false, 19, 12, 5}}};

std::int64_t sw(std::uint16_t word) noexcept {
    return word < 32768U ? word : static_cast<std::int64_t>(word) - 65536;
}
std::int64_t floor256(std::int64_t value) noexcept {
    return value >= 0 ? value / 256 : -((-value + 255) / 256);
}
VmfTransform reference(const VmfTransform& parent, const VmfTransform& local,
                       std::uint32_t previous) noexcept {
    std::array<std::uint32_t, 20> regs{};
    for (std::size_t i = 0; i < 6; ++i) {
        regs[i] = parent.matrix_bits[i];
        regs[6 + i] = local.matrix_bits[i];
    }
    for (const auto& op : program)
        regs[op.dst] = op.multiply
                           ? review_ordered_mul(regs[op.lhs], regs[op.rhs])
                           : review_ordered_add(regs[op.lhs], regs[op.rhs]);
    VmfTransform out;
    for (std::size_t i = 0; i < 6; ++i)
        out.matrix_bits[i] = regs[14 + i];
    for (std::size_t i = 0; i < 4; ++i) {
        out.color_words[i] = static_cast<std::uint16_t>(
            floor256(sw(parent.color_words[i]) * sw(local.color_words[i])));
        out.color_words[4 + i] = static_cast<std::uint16_t>(
            floor256(sw(parent.color_words[i]) * sw(local.color_words[4 + i])) +
            sw(parent.color_words[4 + i]));
    }
    const auto active = parent.flags | local.flags;
    out.flags = (previous ^ ((~active) & 3U)) | (active & 3U);
    return out;
}

std::uint32_t random_bits(std::uint32_t& state) noexcept {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
bool nan(std::uint32_t bits) noexcept {
    return (bits & 0x7fffffffU) > 0x7f800000U;
}
unsigned mxcsr_mask() noexcept {
    alignas(16) std::array<unsigned char, 512> bytes{};
    _fxsave(bytes.data());
    unsigned mask = 0;
    for (unsigned i = 0; i < 4; ++i)
        mask |= unsigned(bytes[28 + i]) << (8 * i);
    return mask ? mask : 0xffbfU;
}
struct Observation {
    unsigned control{}, status_reference{}, status_actual{};
    std::size_t case_index{};
    VmfTransform parent, local, expected, actual;
};
} // namespace

int main() {
    constexpr std::array<std::uint32_t, 20> special{
        0,           0x80000000U, 0x3f800000U, 0xbf800000U, 0x7f800000U,
        0xff800000U, 0x7fc01234U, 0x7fc05678U, 0xffc0abcdU, 0x7f801234U,
        0xff801235U, 1,           0x80000001U, 0x00800000U, 0x80800000U,
        0x7f7fffffU, 0xff7fffffU, 0x3f7fffffU, 0x3f800001U, 0x3eaaaaabU};
    const unsigned saved = _mm_getcsr();
    const unsigned supported = mxcsr_mask();
    const unsigned base = (saved | 0x1f80U) & ~0xe07fU;
    std::array<Observation, 6> first{};
    std::size_t first_count = 0, total = 0, matrix_cases = 0,
                matrix_components = 0, finite_component_differences = 0,
                color_cases = 0, flag_cases = 0, status_cases = 0,
                controls_changed = 0;
    for (unsigned rc = 0; rc < 4; ++rc) {
        for (unsigned flush = 0; flush < 4; ++flush) {
            const unsigned f =
                (flush & 1 ? 0x40U : 0U) | (flush & 2 ? 0x8000U : 0U);
            if ((supported & f) != f)
                continue;
            const unsigned csr = base | (rc << 13) | f;
            std::uint32_t seed = 0x89abcdefU;
            for (std::size_t k = 0; k < 8192; ++k) {
                VmfTransform p, l;
                for (std::size_t i = 0; i < 6; ++i) {
                    p.matrix_bits[i] =
                        k < 128 ? special[(k + 3 * i) % special.size()]
                                : random_bits(seed);
                    l.matrix_bits[i] =
                        k < 128 ? special[(7 * k + 5 * i + 1) % special.size()]
                                : random_bits(seed);
                }
                // A direct two-qNaN discriminator, not merely a NaN class
                // check.
                if (k == 0) {
                    p = VmfTransform{};
                    l = VmfTransform{};
                    p.matrix_bits[0] = 0x7fc01234U;
                    l.matrix_bits[0] = 0x7fc05678U;
                }
                for (std::size_t i = 0; i < 8; ++i) {
                    p.color_words[i] =
                        static_cast<std::uint16_t>(random_bits(seed));
                    l.color_words[i] =
                        static_cast<std::uint16_t>(random_bits(seed));
                }
                p.flags = random_bits(seed);
                l.flags = random_bits(seed);
                const auto previous = random_bits(seed);
                _mm_setcsr(csr);
                const auto expected = reference(p, l, previous);
                const auto refcsr = _mm_getcsr();
                _mm_setcsr(csr);
                const auto actual =
                    tdur::ui::compose_vmf_transform(p, l, previous);
                const auto actcsr = _mm_getcsr();
                bool md = false;
                for (std::size_t i = 0; i < 6; ++i) {
                    if (expected.matrix_bits[i] != actual.matrix_bits[i]) {
                        md = true;
                        ++matrix_components;
                        if (!nan(expected.matrix_bits[i]) ||
                            !nan(actual.matrix_bits[i]))
                            ++finite_component_differences;
                    }
                }
                if (md)
                    ++matrix_cases;
                if (expected.color_words != actual.color_words)
                    ++color_cases;
                if (expected.flags != actual.flags)
                    ++flag_cases;
                if ((refcsr & 0x3fU) != (actcsr & 0x3fU))
                    ++status_cases;
                if ((actcsr & ~0x3fU) != (csr & ~0x3fU))
                    ++controls_changed;
                if ((md || expected.color_words != actual.color_words ||
                     expected.flags != actual.flags ||
                     (refcsr & 0x3fU) != (actcsr & 0x3fU)) &&
                    first_count < first.size())
                    first[first_count++] = {
                        csr, refcsr & 0x3fU, actcsr & 0x3fU, k, p,
                        l,   expected,       actual};
                ++total;
            }
        }
    }
    _mm_setcsr(saved);
    std::cout << "pointer_bits=" << sizeof(void*) * 8 << " total=" << total
              << " matrix_cases=" << matrix_cases
              << " matrix_components=" << matrix_components
              << " non_nan_component_differences="
              << finite_component_differences << " color_cases=" << color_cases
              << " flag_cases=" << flag_cases
              << " status_cases=" << status_cases
              << " controls_changed=" << controls_changed << '\n';
    for (std::size_t j = 0; j < first_count; ++j) {
        const auto& x = first[j];
        std::cout << "mismatch case=" << x.case_index << " control=" << std::hex
                  << x.control << " status_expected=" << x.status_reference
                  << " status_actual=" << x.status_actual << '\n';
        for (const auto* a : {&x.parent.matrix_bits, &x.local.matrix_bits,
                              &x.expected.matrix_bits, &x.actual.matrix_bits}) {
            for (auto v : *a)
                std::cout << std::setw(8) << std::setfill('0') << v << ' ';
            std::cout << '\n';
        }
        std::cout << std::dec;
    }
    return matrix_cases || color_cases || flag_cases || status_cases ||
                   controls_changed
               ? 1
               : 0;
}
