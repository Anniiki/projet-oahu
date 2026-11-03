#include "vmf_property_math.hpp"

#include <bit>
#include <cmath>
#include <xmmintrin.h>

namespace tdur::ui::detail {
namespace {
float component(const VmfTransform &local, std::size_t index) noexcept {
    return std::bit_cast<float>(local.matrix_bits[index]);
}
double norm(float a, float b) noexcept {
    const double x = a;
    const double y = b;
    const double xx = x * x;
    const double yy = y * y;
    return std::sqrt(xx + yy);
}
double orientation(const VmfTransform &local) noexcept {
    // 442C70 stores each candidate norm to binary32 BEFORE DIVSS. Its acos
    //  result is multiplied by57.295776f.442D00/20 feed that degree-valued
    //  result straight into FCOS/FSIN; do not silently replace it by atan2
    //  or insert a radians conversion which the observed code does not have.
    auto numerator = component(local, 0);
    auto length = static_cast<float>(norm(numerator, component(local, 2)));
    if (length == 0.0f) {
        numerator = component(local, 1);
        length = static_cast<float>(norm(numerator, component(local, 3)));
        if (length == 0.0f)
            return 0.0;
    }
    const auto ratio =
        _mm_cvtss_f32(_mm_div_ss(_mm_set_ss(numerator), _mm_set_ss(length)));
    return std::acos(static_cast<double>(ratio)) *
           static_cast<double>(std::bit_cast<float>(0x42652ee0U));
}
double native_cos(double angle) noexcept {
    // x87 leaves a finite out-of-range argument on its stack (C2). This is
    // observable for huge rotation operands; it is not trig range reduction.
    if (std::isfinite(angle) && std::abs(angle) >= 0x1p63)
        return angle;
    return std::cos(angle);
}
double native_sin(double angle) noexcept {
    if (std::isfinite(angle) && std::abs(angle) >= 0x1p63)
        return angle;
    return std::sin(angle);
}
void store(VmfTransform &local, std::size_t index, double value) noexcept {
    local.matrix_bits[index] =
        std::bit_cast<std::uint32_t>(static_cast<float>(value));
}
} // namespace

void set_vmf_property_x_scale(VmfTransform &local, float value) noexcept {
    const auto angle = orientation(local);
    store(local, 0, native_cos(angle) * static_cast<double>(value));
    store(local, 2, native_sin(angle) * static_cast<double>(value));
}
void set_vmf_property_y_scale(VmfTransform &local, float value) noexcept {
    const auto angle = orientation(local);
    store(local, 1, native_cos(angle) * static_cast<double>(value));
    store(local, 3, -(native_sin(angle) * static_cast<double>(value)));
}
void set_vmf_property_rotation(VmfTransform &local, float degrees) noexcept {
    const auto x_length = norm(component(local, 0), component(local, 2));
    const auto y_length = norm(component(local, 1), component(local, 3));
    const auto angle = static_cast<double>(degrees) *
                       static_cast<double>(std::bit_cast<float>(0x3c8efa35U));
    const auto cosine = native_cos(angle);
    // 442D40 rounds the sine to binary32 before multiplying the two norms.
    const auto sine = static_cast<float>(native_sin(angle));
    store(local, 0, cosine * x_length);
    store(local, 1, cosine * y_length);
    store(local, 2, static_cast<double>(sine) * x_length);
    store(local, 3, -(static_cast<double>(sine) * y_length));
}
} // namespace tdur::ui::detail
