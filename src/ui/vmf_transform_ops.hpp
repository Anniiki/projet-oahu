#pragma once

#include <cstdint>

namespace tdur::ui::detail {

// Separate translation unit, with IPO disabled on tdur_vmf_transform. The
// calls are deliberately opaque: even /fp:strict permits MSVC to reorder
// intrinsics, changing the first exception when several traps are unmasked.
[[nodiscard]] std::uint32_t
multiply_vmf_transform_bits(std::uint32_t lhs, std::uint32_t rhs) noexcept;
[[nodiscard]] std::uint32_t add_vmf_transform_bits(std::uint32_t lhs,
                                                   std::uint32_t rhs) noexcept;

} // namespace tdur::ui::detail
