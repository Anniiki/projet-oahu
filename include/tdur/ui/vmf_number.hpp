#pragma once

#include <string_view>

namespace tdur::ui {

// Secondary PC 0x00442530..0x004426eb: 435 body bytes, 444-byte span.
// See docs/evidence/vmf_number.md for identity, evidence and primary limits.
// This is the VMF player's byte-string conversion, not strtof or SWF's
// general Number(): any lowercase 'e' before the first NUL returns +0;
// only an initial sign and the initial run of digits/dots are considered.
// Integer contributions are accumulated right-to-left, fractional ones
// left-to-right, with binary32 operations in their observed order.
//
// Input is borrowed for this call only. The first NUL ends the string;
// without one, the view's end is the terminator. No allocation, out-of-view
// read, locale lookup or floating-point environment change is performed.
// Uses the caller's floating-point environment, as the native SSE code does.
// Overflow/nonfinite results and unusual repeated-dot behavior are preserved.
[[nodiscard]] float vmf_number_from_string(std::string_view text) noexcept;

} // namespace tdur::ui
