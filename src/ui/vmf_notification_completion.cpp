#include "tdur/ui/vmf_notification_completion.hpp"

#include <bit>

namespace tdur::ui {
namespace {
// Positive value = significand * 2^exponent. This path needs at most62 bits:
// uint32 times the24-bit scale significand, then times25 (100 =25*2^2).
struct Binary {
    std::uint64_t significand;
    int exponent;
};
Binary rounded(Binary value, unsigned precision, unsigned mode) noexcept {
    const auto bits = static_cast<unsigned>(std::bit_width(value.significand));
    if (bits <= precision)
        return value;
    const auto removed = bits - precision;
    const auto tail = value.significand & ((std::uint64_t{1} << removed) - 1);
    auto kept = value.significand >> removed;
    bool increment = mode == 2 && tail != 0;
    if (mode == 0) {
        const auto half = std::uint64_t{1} << (removed - 1);
        increment = tail > half || (tail == half && (kept & 1));
    }
    value.significand = kept + static_cast<unsigned>(increment);
    value.exponent += static_cast<int>(removed);
    return value;
}
std::uint32_t binary32(Binary value) noexcept {
    if (!value.significand)
        return 0;
    const auto leading =
        static_cast<int>(std::bit_width(value.significand)) - 1;
    const auto normalized = leading >= 23 ? value.significand >> (leading - 23)
                                          : value.significand << (23 - leading);
    const auto exponent =
        static_cast<std::uint32_t>(leading + value.exponent + 127);
    return (exponent << 23) |
           (static_cast<std::uint32_t>(normalized) & 0x7fffffU);
}
std::int32_t floor_integer(std::uint32_t bits) noexcept {
    const auto exponent = static_cast<int>((bits >> 23) & 0xffU) - 127;
    if (exponent < 0)
        return 0;
    const auto significand = (bits & 0x7fffffU) | 0x800000U;
    // The path's maximum is below2^30; every integer fits positive int32.
    const auto whole = exponent >= 23 ? significand << (exponent - 23)
                                      : significand >> (23 - exponent);
    return static_cast<std::int32_t>(whole);
}
} // namespace

std::expected<VmfNotificationCompletion, VmfNotificationCompletionError>
compute_vmf_notification_completion(std::uint32_t sum,
                                    std::uint16_t control) noexcept {
    const auto pc = (control >> 8) & 3U;
    if (pc == 1)
        return std::unexpected(
            VmfNotificationCompletionError::reserved_precision);
    if ((control & 0x3fU) != 0x3fU)
        return std::unexpected(
            VmfNotificationCompletionError::unmasked_exceptions);
    const unsigned precision = pc == 0 ? 24U : pc == 2 ? 53U : 64U;
    const auto mode = (control >> 10) & 3U;
    Binary value{sum, 0};
    // FILD is exact. Only the negative signed input path performs FADD;
    // its positive uint32 result is rounded to PC before the first FMUL.
    if (sum & 0x80000000U)
        value = rounded(value, precision, mode);
    value.significand *= 0x83126fU; // float bits3A83126F =83126F *2^-33.
    value.exponent -= 33;
    value = rounded(value, precision, mode);
    value.significand *= 25;
    value.exponent += 2; // float100 =25 *2^2.
    value = rounded(value, precision, mode);
    const auto stored = binary32(rounded(value, 24, mode));
    // 40C1A0 widens this exact binary32 value to double then floors it.
    //  The resulting integral value is exact even in PC24;674EB4 returns it.
    return VmfNotificationCompletion{stored, floor_integer(stored)};
}

} // namespace tdur::ui
