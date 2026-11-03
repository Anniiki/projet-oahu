#include "fixtures/vmf_transform_vectors.hpp"
#include "tdur/ui/vmf_transform.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <intrin.h>
#include <iostream>
#include <new>
#include <type_traits>
#include <xmmintrin.h>

namespace transform_allocations {
thread_local bool enabled{};
thread_local std::size_t calls{};
void* allocate(std::size_t bytes) {
    if (enabled)
        ++calls;
    if (auto* memory = std::malloc(bytes ? bytes : 1))
        return memory;
    throw std::bad_alloc();
}
} // namespace transform_allocations
void* operator new(std::size_t bytes) {
    return transform_allocations::allocate(bytes);
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}

namespace {
using tdur::ui::compose_vmf_transform;
using tdur::ui::reset_vmf_transform;
using tdur::ui::VmfTransform;
std::size_t checks{}, failures{}, daz_tests{}, ftz_tests{};

void check(bool value, const char* message, const char* label = "") {
    ++checks;
    if (!value) {
        ++failures;
        if (failures <= 30)
            std::cerr << "FAIL: " << message << ' ' << label << '\n';
    }
}
bool is_nan(std::uint32_t bits) noexcept {
    return (bits & 0x7fffffffU) > 0x7f800000U;
}
struct Environment {
    unsigned saved = _mm_getcsr();
    ~Environment() noexcept { _mm_setcsr(saved); }
    unsigned controlled(unsigned mode = 0) const noexcept {
        // All six exceptions masked, status clear, DAZ/FTZ off, selected RC.
        return ((saved | 0x1f80U) & ~0xe07fU) | (mode << 13);
    }
};
static_assert(sizeof(VmfTransform) == 44);
static_assert(std::is_trivially_copyable_v<VmfTransform>);
static_assert(noexcept(compose_vmf_transform(VmfTransform{}, VmfTransform{})));
static_assert(noexcept(reset_vmf_transform(std::declval<VmfTransform&>())));

VmfTransform observed_compose(const VmfTransform& parent,
                              const VmfTransform& local,
                              std::uint32_t previous = 0,
                              const char* label = "") {
    const auto before_parent = parent, before_local = local;
    const auto before_csr = _mm_getcsr();
    transform_allocations::calls = 0;
    transform_allocations::enabled = true;
    const auto result = compose_vmf_transform(parent, local, previous);
    const auto after_csr = _mm_getcsr();
    transform_allocations::enabled = false;
    check(transform_allocations::calls == 0,
          "composition performs no ordinary new allocation", label);
    check(parent == before_parent && local == before_local,
          "both input values are unchanged", label);
    check((after_csr & ~0x3fU) == (before_csr & ~0x3fU),
          "caller MXCSR control bits are unchanged", label);
    check((after_csr & before_csr & 0x3fU) == (before_csr & 0x3fU),
          "caller preexisting sticky status is not cleared", label);
    return result;
}
void matrix_vectors(const Environment& environment) {
    for (unsigned mode = 0; mode != 4; ++mode) {
        for (const auto& row : vmf_transform_fixture::matrices) {
            _mm_setcsr(environment.controlled(mode) | 0x21U);
            VmfTransform parent, local;
            parent.matrix_bits = row.parent;
            local.matrix_bits = row.local;
            const auto result = observed_compose(parent, local, 0, row.name);
            check(result.flags == 3,
                  "two absent input flags TOGGLE zero destination", row.name);
            check(result.color_words == VmfTransform{}.color_words,
                  "identity colors survive matrix-only composition", row.name);
            for (std::size_t component = 0; component < 6; ++component) {
                const auto expected = row.expected[mode][component];
                check(is_nan(expected)
                          ? is_nan(result.matrix_bits[component])
                          : result.matrix_bits[component] == expected,
                      "exact rational separately rounded MULSS/ADDSS result",
                      row.name);
            }
            if (row.parent == row.local) {
                _mm_setcsr(environment.controlled(mode) | 0x21U);
                const auto alias =
                    observed_compose(parent, parent, 2, row.name);
                check(alias.flags == 1,
                      "same-reference compose preserves destination XOR rule",
                      row.name);
                for (std::size_t component = 0; component < 6; ++component) {
                    const auto expected = row.expected[mode][component];
                    check(is_nan(expected)
                              ? is_nan(alias.matrix_bits[component])
                              : alias.matrix_bits[component] == expected,
                          "same object may supply both immutable source "
                          "references",
                          row.name);
                }
            }
        }
    }
}
void color_vectors(const Environment& environment) {
    for (unsigned mode = 0; mode != 4; ++mode) {
        _mm_setcsr(environment.controlled(mode) | 0x3fU);
        for (const auto& row : vmf_transform_fixture::colors) {
            VmfTransform parent, local;
            parent.color_words = row.parent;
            local.color_words = row.local;
            const auto result = observed_compose(parent, local, 0, row.name);
            check(result.color_words == row.expected,
                  "signed16 floor-shift8 and wrap16 color/offset result",
                  row.name);
            check(result.matrix_bits == VmfTransform{}.matrix_bits,
                  "identity matrix is computed independently of colors",
                  row.name);
        }
    }
}
void flag_vectors(const Environment& environment) {
    _mm_setcsr(environment.controlled() | 0x3fU);
    for (const auto& row : vmf_transform_fixture::flags) {
        VmfTransform parent, local;
        parent.flags = row.parent;
        local.flags = row.local;
        const auto result = observed_compose(parent, local, row.previous);
        check(result.flags == row.expected,
              "destination flags use previous, OR active inputs and XOR absent "
              "bits");
        check(result.matrix_bits == VmfTransform{}.matrix_bits &&
                  result.color_words == VmfTransform{}.color_words,
              "unrecognized source flag bits do not skip value computations");
    }
    VmfTransform parent, local;
    const auto first = observed_compose(parent, local);
    const auto second = observed_compose(parent, local, first.flags);
    const auto third = observed_compose(parent, local, second.flags);
    check(first.flags == 3 && second.flags == 0 && third.flags == 3,
          "recomputation of inactive flags deliberately alternates3/0/3");
}
void reset_values(const Environment& environment) {
    const VmfTransform identity;
    check(identity.flags == 0 &&
              identity.color_words == std::array<std::uint16_t, 8>{256, 256,
                                                                   256, 256, 0,
                                                                   0, 0, 0} &&
              identity.matrix_bits == std::array<std::uint32_t, 6>{0x3f800000,
                                                                   0x3f800000,
                                                                   0, 0, 0, 0},
          "default construction is exact native identity");
    for (unsigned mode = 0; mode != 4; ++mode) {
        VmfTransform poisoned;
        poisoned.flags = 0xffffffff;
        poisoned.color_words.fill(0x9abc);
        poisoned.matrix_bits.fill(0xff801234);
        const auto csr = environment.controlled(mode) | 0x3fU;
        _mm_setcsr(csr);
        transform_allocations::calls = 0;
        transform_allocations::enabled = true;
        reset_vmf_transform(poisoned);
        const auto after = _mm_getcsr();
        transform_allocations::enabled = false;
        check(poisoned == identity,
              "reset replaces every flag/color/matrix field");
        check(transform_allocations::calls == 0,
              "reset performs no ordinary new allocation");
        check(after == csr,
              "reset preserves exact caller MXCSR, including status");
    }
}
void exact_unmasked_controls(const Environment& environment) {
    // Identity uses only exact finite MUL/ADD operations: safely verify each
    // caller mask bit can stay UNMASKED without provoking any exception.
    for (unsigned exception = 0; exception < 6; ++exception) {
        const auto csr = environment.controlled() & ~(1U << (7 + exception));
        _mm_setcsr(csr);
        const VmfTransform parent, local;
        const auto result = observed_compose(parent, local);
        check(
            result.matrix_bits == VmfTransform{}.matrix_bits &&
                _mm_getcsr() == csr,
            "exact finite composition preserves caller unmasked exception bit");
    }
}
std::uint32_t mxcsr_supported_mask() noexcept {
    alignas(16) std::array<unsigned char, 512> fx{};
    _fxsave(fx.data());
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i)
        result |= std::uint32_t(fx[28 + i]) << (8 * i);
    // Architectural legacy mask when FXSAVE reports no MXCSR_MASK value.
    return result ? result : 0xffbfU;
}
void daz_ftz_controls(const Environment& environment) {
    const auto supported = mxcsr_supported_mask();
    if (supported & 0x40U) {
        VmfTransform parent, local;
        parent.matrix_bits[0] = 1;
        _mm_setcsr(environment.controlled());
        const auto gradual = observed_compose(parent, local);
        _mm_setcsr(environment.controlled() | 0x40U);
        const auto flushed_input = observed_compose(parent, local);
        check(gradual.matrix_bits[0] == 1 && flushed_input.matrix_bits[0] == 0,
              "DAZ input policy is caller-selected, not forced by helper");
        ++daz_tests;
    }
    if (supported & 0x8000U) {
        VmfTransform parent, local;
        parent.matrix_bits[0] = 0x00800000;
        local.matrix_bits[0] = 0x3eaaaaab;
        _mm_setcsr(environment.controlled());
        const auto gradual = observed_compose(parent, local);
        _mm_setcsr(environment.controlled() | 0x8000U);
        const auto flushed_output = observed_compose(parent, local);
        check(gradual.matrix_bits[0] == 0x002aaaab &&
                  flushed_output.matrix_bits[0] == 0,
              "FTZ inexact tiny result policy is caller-selected");
        ++ftz_tests;
    }
}
struct PayloadObservation {
    std::uint32_t parent, local, output, status;
};
std::array<PayloadObservation, 3>
nan_observations(const Environment& environment) {
    std::array<PayloadObservation, 3> observations{
        {{0x7fc01234, 0x7fc05678, 0, 0},
         {0xffc01234, 0x3f800000, 0, 0},
         {0x7f801234, 0x3f800000, 0, 0}}};
    for (auto& row : observations) {
        VmfTransform parent, local;
        parent.matrix_bits[0] = row.parent;
        local.matrix_bits[0] = row.local;
        _mm_setcsr(environment.controlled());
        const auto output = observed_compose(parent, local);
        row.output = output.matrix_bits[0];
        row.status = _mm_getcsr() & 0x3fU;
        check(is_nan(row.output), "nonfinite inputs accepted; NaN class "
                                  "checked without payload oracle");
        if (row.parent == 0x7f801234)
            check((row.status & 1U) != 0,
                  "signaling NaN raises masked invalid status through SSE");
    }
    return observations;
}
} // namespace

int main() {
    std::array<PayloadObservation, 3> observations{};
    {
        Environment environment;
        reset_values(environment);
        matrix_vectors(environment);
        color_vectors(environment);
        flag_vectors(environment);
        exact_unmasked_controls(environment);
        daz_ftz_controls(environment);
        observations = nan_observations(environment);
    }
    // These payload values are observations of our source-owned helper on the
    // current CPU, not Fraction-oracle expectations or native game execution.
    for (const auto& row : observations)
        std::cout << "NaN observation parent=" << std::hex << row.parent
                  << " local=" << row.local << " output=" << row.output
                  << " mxcsr_status=" << row.status << std::dec << '\n';
    std::cout << "VMF transform: " << checks << " checks, " << failures
              << " failures; " << vmf_transform_fixture::matrices.size()
              << " matrix cases x4 DAZ/FTZ-off modes, " << daz_tests
              << " DAZ case, " << ftz_tests << " FTZ case\n";
    return failures == 0 ? 0 : 1;
}
