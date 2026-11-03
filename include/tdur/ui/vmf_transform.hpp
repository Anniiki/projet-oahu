#pragma once

#include <array>
#include <cstdint>

namespace tdur::ui {

// Native 44-byte local/composed transform, expressed without native pointers.
// Color words retain the signed16 bit patterns. Matrix order is a,d,b,c,tx,ty:
// x' = a*x + c*y + tx, y' = b*x + d*y + ty. Default is 442E20's identity.
struct VmfTransform {
    std::uint32_t flags{};
    std::array<std::uint16_t, 8> color_words{256, 256, 256, 256, 0, 0, 0, 0};
    std::array<std::uint32_t, 6> matrix_bits{0x3f800000, 0x3f800000, 0,
                                             0,          0,          0};
    friend bool operator==(const VmfTransform&, const VmfTransform&) = default;
};

// 442E20: resets every field, including flags. No allocation or I/O.
void reset_vmf_transform(VmfTransform& transform) noexcept;

// 442E80 + 4429F0: compose parent * local, irrespective of active flags.
// SSE binary32 operations retain the native operand/evaluation order and use
// the caller's MXCSR (rounding, DAZ/FTZ and exception masks/status). No FMA.
// Color operations sign-extend16, floor-divide products by256, then wrap16;
// they do not clamp to a display color range.
//
// Destination flags are stateful in the native routine: for each bit1/2,
// either input having the bit sets it; otherwise it is TOGGLED, not cleared.
// All other previous_flags bits survive. Supply the previous destination's
// flags when recomputing it. This behavior is retained rather than repaired.
//
// Inputs are immutable; the value result avoids native output alias hazards.
// Arbitrary matrix bits are accepted, including nonfinite values. This helper
// does not establish finite render bounds, execute placements or render UI.
// See docs/evidence/vmf_transform.md for evidence and validation scope.
[[nodiscard]] VmfTransform
compose_vmf_transform(const VmfTransform& parent, const VmfTransform& local,
                      std::uint32_t previous_flags = 0) noexcept;

} // namespace tdur::ui
