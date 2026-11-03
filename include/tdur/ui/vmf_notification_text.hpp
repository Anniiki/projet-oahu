#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::ui {

enum class VmfNotificationTextError {
    source_range,
    output_range,
    overlapping_storage,
    invalid_context,
    name_limit,
};
struct VmfNotificationByteEncoding {
    std::size_t consumed{}, written{};
    bool terminated{};
};

// 451C90: expand individual high bytes to two UTF8 bytes. This is not UTF8
// normalization or a Windows codepage conversion. initial_guard<=0 returns0;
// a positive guard is NOT the output capacity. source_limit counts SOURCE
// bytes, signed; -1 becomes INT32_MAX. A NUL, when written, counts in written.
// The native loop also reads the following source byte after each payload.
// Owned spans must cover those reads and all output; overlap is outside this
// safe domain. Modern validation errors leave output unchanged. No allocation.
[[nodiscard]] std::expected<VmfNotificationByteEncoding,
                            VmfNotificationTextError>
encode_vmf_notification_bytes(std::span<char> destination,
                              std::span<const char> source,
                              std::int32_t initial_guard,
                              std::int32_t source_limit = -1) noexcept;

struct VmfNotificationTextSlot {
    std::array<char, 18> key{};
    std::array<char, 64> value{};
    bool operator==(const VmfNotificationTextSlot&) const = default;
};
struct VmfNotificationTextParameters {
    std::array<VmfNotificationTextSlot, 10> slots{};
    bool operator==(const VmfNotificationTextParameters&) const = default;
};
enum class VmfNotificationTextBindStatus { ignored, full, inserted };
struct VmfNotificationTextBindReport {
    VmfNotificationTextBindStatus status;
    std::optional<std::size_t> slot;
};

// 716D30: optional views model NULL C-string arguments; otherwise each view
// supplies a C-string prefix (an embedded NUL ends it). Guards precede context
// access. Insert in the FIRST empty key, with the actual converted C2A7 suffix;
// never update an equal existing key. Full table is a no-op. Only an available
// slot requires name<=15 bytes; longer names are a modern error, not native
// truncation. Value keeps63 bytes plus NUL, with strncpy-style zero padding.
// Inputs must not alias the selected destination slot. Access is serialized.
[[nodiscard]] std::expected<VmfNotificationTextBindReport,
                            VmfNotificationTextError>
bind_vmf_notification_text(VmfNotificationTextParameters* parameters,
                           std::optional<std::string_view> name,
                           std::optional<std::string_view> value) noexcept;

// 716CF0: clear ONLY key[0] and value[0] in each of10 slots. Tail bytes stay.
void clear_vmf_notification_text(
    VmfNotificationTextParameters& parameters) noexcept;

} // namespace tdur::ui
