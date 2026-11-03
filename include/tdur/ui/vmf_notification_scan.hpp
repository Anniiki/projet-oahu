#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

namespace tdur::ui {

enum class VmfNotificationScanErrorCode {
    source_range,
    integer_range,
    unknown_scan_value,
    delimiter_encoding,
};
struct VmfNotificationScanError {
    VmfNotificationScanErrorCode code;
    // Cursor of a refused advance/conversion; no original process pointer.
    std::size_t offset;
};

// 716F60: read-only prepass using the encoded A7 delimiter and the original
// 1/2/3-byte steps (other leading patterns use1). It is not a UTF8 decoder.
// The backing is stable/readable and accesses serialized. Span end implies
// one virtual NUL; advancing beyond it is a modern source_range refusal.
// An observed NUL ends the outer scan; the inner closing-marker search can
// cross a NUL INSIDE supplied backing, as the native loop does.
//
// Return value -1 is a NATIVE ordering-gap result, not a modern error. Counts
// admitted by parsed<=9 are0..9; the native final clamp10 is unreachable in
// this defined domain. Only parsed==count+1 advances count; zero, negatives,
// values>9 and nonnumeric tokens are not automatically ordering errors.
// Numeric prefixes use defined ASCII %d behavior (space/sign/digits), with
// leading zeros retained semantically. Locale-specific CRT behavior and
// signed conversion overflow are not inferred. EOF retains a known prior
// scanf temporary; its initially unknown native value is refused explicitly.
// MSVCR71 also returns EOF for a sign immediately followed by NUL.
// No allocation, output mutation or reinterpretation of x86 variadic pointers.
[[nodiscard]] std::expected<std::int32_t, VmfNotificationScanError>
scan_vmf_notification_tokens(std::span<const char> source) noexcept;

} // namespace tdur::ui
