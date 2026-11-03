#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>

namespace tdur::ui {

// Native layout strides and offsets
inline constexpr std::uint32_t kRecordStride = 0x1f8U; // 504 bytes
inline constexpr std::uint32_t kRecordKeyOffset = 0x1f0U;

inline constexpr std::uint32_t kNameStride = 0x94U; // 148 bytes
inline constexpr std::uint32_t kNameKeyOffset = 0x00U;
inline constexpr std::uint32_t kNameTextOffset = 0x0cU;

inline constexpr std::uint32_t kGroupStride = 0x98U; // 152 bytes
inline constexpr std::uint32_t kGroupTextOffset = 0x10U;
inline constexpr std::uint32_t kGroupRefBaseOffset = 0x18U;
inline constexpr std::uint32_t kGroupRefStride = 0x08U;
inline constexpr std::size_t kGroupReferenceSlots = 15U;

// Logical projections of the observed fields, not native ABI layouts. Keys,
// rows, references and string views borrow stable caller-owned storage.
// Record returned by 9A9480 and referenced by 9A9340.
struct DatabaseRecord {
    std::optional<std::uint32_t> key_1f0;
};

// Row returned by 9A92B0. Text at offset +0Ch is inline storage in the row.
struct DatabaseNameRow {
    std::optional<std::uint32_t> key_00;
    std::optional<std::string_view> text_0c;
};

// Row returned by 9A9340. Text at offset +10h is a C-string pointer in the
// native row.
struct DatabaseGroupRow {
    // Outer absent = unknown binding; inner absent = confirmed NULL pointer;
    // value = valid text.
    std::optional<std::optional<std::string_view>> text_10;
    // 15 references at offsets 18h, 20h, 28h, 30h, 38h, 40h, 48h, 50h, 58h,
    // 60h, 68h, 70h, 78h, 80h, 88h.
    std::array<std::optional<const DatabaseRecord*>, kGroupReferenceSlots>
        references_18_88{};
};

// Explicit DWORD coordinates are unrelated to host pointers. Spans borrow
// storage; serialize reads and keep every reached row/reference alive.
struct DatabaseRecordTable {
    std::optional<std::uint32_t> base_address;
    std::optional<std::uint32_t> count;
    std::optional<std::span<const DatabaseRecord>> rows;
};

struct DatabaseNameTable {
    std::optional<std::uint32_t> base_address;
    std::optional<std::uint32_t> count;
    std::optional<std::span<const DatabaseNameRow>> rows;
};

struct DatabaseGroupTable {
    std::optional<std::uint32_t> base_address;
    std::optional<std::uint32_t> count;
    std::optional<std::span<const DatabaseGroupRow>> rows;
};

enum class VmfNotificationDatabaseErrorCode {
    table_unbound,
    unbound_metadata,
    unbound_argument,
    invalid_binding,
    unbound_record_key,
    unbound_reference,
    row_out_of_bounds,
    misaligned_cursor,
    resource_limit,
};

struct VmfNotificationDatabaseError {
    VmfNotificationDatabaseErrorCode code{};
    std::string_view detail;
    std::size_t row{};
    std::size_t slot{};
    std::uint32_t cursor{};
};

// 9A9480 (49B): captures base at +4, count at +5A4, end = base + count * 1F8h.
// Initial unsigned guard base >= end returns nullptr immediately without
// inspecting rows. Iterates by stride 1F8h, compares row+1F0h to key (EDX).
// First match returns pointer. EDX is unmodified, preserving the key for the
// caller.
[[nodiscard]] std::expected<const DatabaseRecord*, VmfNotificationDatabaseError>
find_vmf_notification_record_9a9480(
    const DatabaseRecordTable& table, std::uint32_t key,
    std::uint32_t row_budget = 100'000) noexcept;

// 9A92B0 (48B): captures base at +43E0h, count at +43E4h, end = base + count *
// 94h. Initial unsigned guard base >= end is evaluated BEFORE dereferencing key
// pointer. Empty table ignores unknown/NULL key pointer and rows. Non-empty
// table captures key once from *key_pointer, compares row+00h, stride 94h. Does
// NOT inspect text at +0Ch. First match returns row pointer.
[[nodiscard]] std::expected<const DatabaseNameRow*,
                            VmfNotificationDatabaseError>
find_vmf_notification_name_9a92b0(
    const DatabaseNameTable& table,
    std::optional<const std::uint32_t*> key_pointer,
    std::uint32_t row_budget = 100'000) noexcept;

// 9A9340 (256B): argument Record* in EDX. If NULL, returns nullptr immediately
// BEFORE inspecting the global group table.
// An unknown argument fails at that guard, even if the table is known empty.
// Base at +1Ch, count at +5C0h, end = base + count * 98h.
// Initial unsigned guard base >= end returns nullptr without dereferencing
// argument key. Non-empty table captures key from argument Record+1F0h once.
// For each group row, inspects the 15 references in order (offsets 18h..88h).
// First reference matching the key returns the group row pointer immediately.
// Slots after a match are not inspected. NULL reference yields invalid_binding.
[[nodiscard]] std::expected<const DatabaseGroupRow*,
                            VmfNotificationDatabaseError>
find_vmf_notification_group_9a9340(
    std::optional<const DatabaseGroupTable*> table,
    std::optional<const DatabaseRecord*> record_argument,
    std::uint32_t row_budget = 100'000) noexcept;

} // namespace tdur::ui
