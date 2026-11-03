#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

namespace tdur::ui {

using VmfNotificationRingText = std::array<char, 400>;
struct VmfNotificationSingleRing {
    std::array<VmfNotificationRingText, 8> slots{};
    std::int32_t index{};
};
struct VmfNotificationDoubleRingSlot {
    VmfNotificationRingText first{}, second{};
    bool operator==(const VmfNotificationDoubleRingSlot&) const = default;
};
struct VmfNotificationDoubleRing {
    std::array<VmfNotificationDoubleRingSlot, 8> slots{};
    std::int32_t index{};
};
enum class VmfNotificationRingError {
    invalid_index,
    null_source,
    overlapping_storage,
};

// Logical owned payloads, not native object layout: single slots use native
// stride800 but only400 written bytes. Slot7's trailing native gap contains
// counter320C, so no invented per-slot400-byte opaque tail is stored here.
// Caller establishes a real ring; default storage is modern initialization,
// not a reconstruction of the original owner constructor or global binding.
//
// Both APIs require serialized accesses and stable, live storage. Views are
// bounded C-string prefixes (embedded NUL terminates; view end implies NUL).
// Metadata is fixed at entry, contents are read only at the matching copy.
// Borrowed mutable buffers must expose the full readable window needed after
// prior writes; e.g. a second view into firstdest should cover all400 bytes.
// A NULL optional is an error at its copy phase. Disjoint or exact-self
// copies are supported; other overlap is a modern explicit error.

// 89BBC0: strncpy400, force lastNUL, index advance0..7. Returns native EAX
//  slot offset old_index*800 (zero can mean success), not a BOOL status.
[[nodiscard]] std::expected<std::uint32_t, VmfNotificationRingError>
push_vmf_notification_text_single(
    VmfNotificationSingleRing& ring,
    std::optional<std::string_view> source) noexcept;

// 89BC20: first strncpy400, THEN second strncpy400, THEN both final NULs,
//  THEN advance. Second source may reference the first destination. Errors
//  retain earlier copies but skip later terminators/advance; never replay.
//  Native AL is zero; EAX's remaining pointer bits are not reproduced.
[[nodiscard]] std::expected<void, VmfNotificationRingError>
push_vmf_notification_text_double(
    VmfNotificationDoubleRing& ring, std::optional<std::string_view> first,
    std::optional<std::string_view> second) noexcept;

} // namespace tdur::ui
