#include "tdur/ui/vmf_transform.hpp"
#include "vmf_transform_ops.hpp"

#include <cstddef>

namespace tdur::ui {
namespace {

// Calls into the separate, non-IPO arithmetic unit must stay observable to
// this compiler unit, retaining the native first unmasked exception too.
std::uint32_t multiply(std::uint32_t lhs, std::uint32_t rhs) noexcept {
    return detail::multiply_vmf_transform_bits(lhs, rhs);
}

std::uint32_t add(std::uint32_t lhs, std::uint32_t rhs) noexcept {
    return detail::add_vmf_transform_bits(lhs, rhs);
}

std::int32_t signed_word(std::uint16_t word) noexcept {
    return word < 0x8000U ? static_cast<std::int32_t>(word)
                          : static_cast<std::int32_t>(word) - 65536;
}

std::int32_t color_product(std::uint16_t lhs, std::uint16_t rhs) noexcept {
    // Every signed16 product fits signed32. C++23 signed right shift floors
    // toward negative infinity, matching SAR; division would truncate to0.
    return (signed_word(lhs) * signed_word(rhs)) >> 8;
}

} // namespace

void reset_vmf_transform(VmfTransform& transform) noexcept {
    transform = VmfTransform{};
}

VmfTransform compose_vmf_transform(const VmfTransform& parent,
                                   const VmfTransform& local,
                                   std::uint32_t previous_flags) noexcept {
    VmfTransform result;
    const auto& p = parent.matrix_bits;
    const auto& l = local.matrix_bits;
    auto& output = result.matrix_bits;

    // 442E80 writes a,c,tx,b,d,ty in this order. Separate statements preserve
    // each MULSS/ADDSS boundary, operand order and translation association.
    const auto a_cross = multiply(p[3], l[2]);
    const auto a_direct = multiply(p[0], l[0]);
    output[0] = add(a_cross, a_direct);

    const auto c_first = multiply(p[0], l[3]);
    const auto c_second = multiply(p[3], l[1]);
    output[3] = add(c_first, c_second);

    const auto x_first = multiply(p[0], l[4]);
    const auto x_second = multiply(p[3], l[5]);
    const auto x_local = add(x_first, x_second);
    output[4] = add(x_local, p[4]);

    const auto b_first = multiply(p[1], l[2]);
    const auto b_second = multiply(p[2], l[0]);
    output[2] = add(b_first, b_second);

    const auto d_cross = multiply(p[2], l[3]);
    const auto d_direct = multiply(p[1], l[1]);
    output[1] = add(d_cross, d_direct);

    const auto y_first = multiply(p[1], l[5]);
    const auto y_second = multiply(p[2], l[4]);
    const auto y_local = add(y_first, y_second);
    output[5] = add(y_local, p[5]);

    // 4429F0: the first four words multiply, the last four are offsets.
    // Explicit unsigned conversion keeps modulo16 rather than saturation.
    for (std::size_t i = 0; i < 4; ++i) {
        result.color_words[i] = static_cast<std::uint16_t>(
            color_product(local.color_words[i], parent.color_words[i]));
        const auto offset =
            color_product(local.color_words[i + 4], parent.color_words[i]);
        result.color_words[i + 4] = static_cast<std::uint16_t>(
            offset + signed_word(parent.color_words[i + 4]));
    }

    result.flags = previous_flags;
    const auto present = parent.flags | local.flags;
    for (const std::uint32_t mask : {2U, 1U}) {
        if ((present & mask) != 0)
            result.flags |= mask;
        else
            result.flags ^= mask;
    }
    return result;
}

} // namespace tdur::ui
