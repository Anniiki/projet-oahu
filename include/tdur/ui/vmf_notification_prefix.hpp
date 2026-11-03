#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::ui {

enum class VmfNotificationPrefixErrorCode {
    missing_reference,
    missing_candidate,
    missing_source,
    output_range,
};
struct VmfNotificationPrefixError {
    VmfNotificationPrefixErrorCode code;
    // Comparison read cursor or bytes already copied before a refused write.
    std::size_t offset;
};

// 451AA0, count in EAX:0 only when count is consumed. Otherwise returns-1
// when the current reference byte is NUL, and+1 for any other mismatch.
// This is not lexical strcmp: comparing A/Z and Z/A can both return+1.
// It reads reference[0] even for count0; a nonempty reference then requires
// candidate[0] before testing count. An empty reference never needs candidate.
// View end supplies a virtual NUL; absent optional means an unknown/NULL
// pointer, refused only at its native read phase. Stable serialized storage.
[[nodiscard]] std::expected<std::int32_t, VmfNotificationPrefixError>
compare_vmf_notification_prefix(std::optional<std::string_view> reference,
                                std::optional<std::string_view> candidate,
                                std::uint32_t count) noexcept;

// 451A70, EAX source/ECX output/EDX count: forward bytewise copy until NUL or
// count, returning copied bytes and never writing a terminator or padding.
// Count0 needs no source. With positive count a source NUL needs no output.
// Source view end implies NUL. A short destination is refused at that write,
// retaining previous stores. Live forward overlap is supported, including
// propagation when destination starts inside a mutable source; no snapshot.
// Keep storage stable and accesses serialized; no allocation or callbacks.
[[nodiscard]] std::expected<std::uint32_t, VmfNotificationPrefixError>
copy_vmf_notification_prefix(std::span<char> destination,
                             std::optional<std::string_view> source,
                             std::uint32_t count) noexcept;

} // namespace tdur::ui
