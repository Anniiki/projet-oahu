#include "tdur/ui/vmf_notification_database.hpp"

namespace tdur::ui {

std::expected<const DatabaseRecord*, VmfNotificationDatabaseError>
find_vmf_notification_record_9a9480(const DatabaseRecordTable& table,
                                    std::uint32_t key,
                                    std::uint32_t row_budget) noexcept {
    if (!table.base_address.has_value() || !table.count.has_value()) {
        return std::unexpected(VmfNotificationDatabaseError{
            .code = VmfNotificationDatabaseErrorCode::unbound_metadata,
            .detail = "Record table base_address or count is unbound",
        });
    }

    const std::uint32_t base = *table.base_address;
    const std::uint32_t count = *table.count;
    const std::uint32_t end = base + (count * kRecordStride);

    // Native 0x9a9497-0x9a9499: CMP EAX, ECX; JNC 0x009a94b1 (unsigned base >=
    // end skips all rows)
    if (base >= end) {
        return nullptr;
    }

    std::uint32_t cursor = base;
    std::size_t visited = 0U;

    while (cursor < end) {
        if (visited >= row_budget) {
            return std::unexpected(VmfNotificationDatabaseError{
                .code = VmfNotificationDatabaseErrorCode::resource_limit,
                .detail = "Row budget exhausted during 9A9480 record lookup",
                .row = visited,
                .cursor = cursor,
            });
        }

        const std::uint32_t offset_from_base = cursor - base;
        if ((offset_from_base % kRecordStride) != 0U) {
            return std::unexpected(VmfNotificationDatabaseError{
                .code = VmfNotificationDatabaseErrorCode::misaligned_cursor,
                .detail = "Record cursor is not aligned to 1F8h stride",
                .row = visited,
                .cursor = cursor,
            });
        }

        const std::size_t row_index =
            static_cast<std::size_t>(offset_from_base / kRecordStride);

        if (!table.rows.has_value()) {
            return std::unexpected(VmfNotificationDatabaseError{
                .code = VmfNotificationDatabaseErrorCode::table_unbound,
                .detail = "Record table rows span is unbound at reached row",
                .row = row_index,
                .cursor = cursor,
            });
        }

        if (row_index >= table.rows->size()) {
            return std::unexpected(VmfNotificationDatabaseError{
                .code = VmfNotificationDatabaseErrorCode::row_out_of_bounds,
                .detail = "Record row index exceeds available span capacity",
                .row = row_index,
                .cursor = cursor,
            });
        }

        const auto& row = (*table.rows)[row_index];
        if (!row.key_1f0.has_value()) {
            return std::unexpected(VmfNotificationDatabaseError{
                .code = VmfNotificationDatabaseErrorCode::unbound_record_key,
                .detail = "Record key_1f0 is unbound at reached row",
                .row = row_index,
                .cursor = cursor,
            });
        }

        // Native 0x9a94a0: CMP [EAX + 0x1f0], EDX; JZ return row
        if (*row.key_1f0 == key) {
            return &row;
        }

        cursor += kRecordStride;
        ++visited;
    }

    return nullptr;
}

std::expected<const DatabaseNameRow*, VmfNotificationDatabaseError>
find_vmf_notification_name_9a92b0(
    const DatabaseNameTable& table,
    std::optional<const std::uint32_t*> key_pointer,
    std::uint32_t row_budget) noexcept {
    if (!table.base_address.has_value() || !table.count.has_value()) {
        return std::unexpected(VmfNotificationDatabaseError{
            .code = VmfNotificationDatabaseErrorCode::unbound_metadata,
            .detail = "Name table base_address or count is unbound",
        });
    }

    const std::uint32_t base = *table.base_address;
    const std::uint32_t count = *table.count;
    const std::uint32_t end = base + (count * kNameStride);

    // Native 0x9a92c4-0x9a92c6: CMP EAX, ECX; JNC 0x009a92dd (unsigned base >=
    // end) Evaluated BEFORE dereferencing key pointer [EDX]. Empty table
    // returns NULL safely.
    if (base >= end) {
        return nullptr;
    }

    if (!key_pointer.has_value()) {
        return std::unexpected(VmfNotificationDatabaseError{
            .code = VmfNotificationDatabaseErrorCode::unbound_argument,
            .detail =
                "Key pointer argument is unbound for non-empty name table",
        });
    }

    if (*key_pointer == nullptr) {
        return std::unexpected(VmfNotificationDatabaseError{
            .code = VmfNotificationDatabaseErrorCode::invalid_binding,
            .detail = "Key pointer is NULL for non-empty name table",
        });
    }

    // Native 0x9a92c8: MOV EDX, [EDX] (captured once after initial guard)
    const std::uint32_t target_key = **key_pointer;

    std::uint32_t cursor = base;
    std::size_t visited = 0U;

    while (cursor < end) {
        if (visited >= row_budget) {
            return std::unexpected(VmfNotificationDatabaseError{
                .code = VmfNotificationDatabaseErrorCode::resource_limit,
                .detail = "Row budget exhausted during 9A92B0 name lookup",
                .row = visited,
                .cursor = cursor,
            });
        }

        const std::uint32_t offset_from_base = cursor - base;
        if ((offset_from_base % kNameStride) != 0U) {
            return std::unexpected(VmfNotificationDatabaseError{
                .code = VmfNotificationDatabaseErrorCode::misaligned_cursor,
                .detail = "Name cursor is not aligned to 94h stride",
                .row = visited,
                .cursor = cursor,
            });
        }

        const std::size_t row_index =
            static_cast<std::size_t>(offset_from_base / kNameStride);

        if (!table.rows.has_value()) {
            return std::unexpected(VmfNotificationDatabaseError{
                .code = VmfNotificationDatabaseErrorCode::table_unbound,
                .detail = "Name table rows span is unbound at reached row",
                .row = row_index,
                .cursor = cursor,
            });
        }

        if (row_index >= table.rows->size()) {
            return std::unexpected(VmfNotificationDatabaseError{
                .code = VmfNotificationDatabaseErrorCode::row_out_of_bounds,
                .detail = "Name row index exceeds available span capacity",
                .row = row_index,
                .cursor = cursor,
            });
        }

        const auto& row = (*table.rows)[row_index];
        if (!row.key_00.has_value()) {
            return std::unexpected(VmfNotificationDatabaseError{
                .code = VmfNotificationDatabaseErrorCode::unbound_record_key,
                .detail = "Name row key_00 is unbound at reached row",
                .row = row_index,
                .cursor = cursor,
            });
        }

        // Native 0x9a92d0: CMP [EAX], EDX; JZ return row (does NOT inspect
        // text_0c)
        if (*row.key_00 == target_key) {
            return &row;
        }

        cursor += kNameStride;
        ++visited;
    }

    return nullptr;
}

std::expected<const DatabaseGroupRow*, VmfNotificationDatabaseError>
find_vmf_notification_group_9a9340(
    std::optional<const DatabaseGroupTable*> table,
    std::optional<const DatabaseRecord*> record_argument,
    std::uint32_t row_budget) noexcept {
    // Native 0x9a9340-0x9a9346: TEST EDX, EDX; JZ ret_null
    // Argument Record* NULL returns NULL immediately BEFORE any global table
    // access.
    if (!record_argument.has_value()) {
        return std::unexpected(VmfNotificationDatabaseError{
            .code = VmfNotificationDatabaseErrorCode::unbound_argument,
            .detail = "Record argument is unbound before its NULL guard",
        });
    }
    if (*record_argument == nullptr) {
        return nullptr;
    }

    if (!table.has_value()) {
        return std::unexpected(VmfNotificationDatabaseError{
            .code = VmfNotificationDatabaseErrorCode::table_unbound,
            .detail = "Global group table is unbound",
        });
    }

    if (*table == nullptr) {
        return std::unexpected(VmfNotificationDatabaseError{
            .code = VmfNotificationDatabaseErrorCode::invalid_binding,
            .detail = "Global group table pointer is NULL",
        });
    }

    const auto* group_table = *table;
    if (!group_table->base_address.has_value() ||
        !group_table->count.has_value()) {
        return std::unexpected(VmfNotificationDatabaseError{
            .code = VmfNotificationDatabaseErrorCode::unbound_metadata,
            .detail = "Group table base_address or count is unbound",
        });
    }

    const std::uint32_t base = *group_table->base_address;
    const std::uint32_t count = *group_table->count;
    const std::uint32_t end = base + (count * kGroupStride);

    // Native 0x9a935f-0x9a9361: CMP EAX, ESI; JNC 0x009a943c (unsigned base >=
    // end) Skips argument key dereference if table is empty.
    if (base >= end) {
        return nullptr;
    }

    const auto* record = *record_argument;

    if (!record->key_1f0.has_value()) {
        return std::unexpected(VmfNotificationDatabaseError{
            .code = VmfNotificationDatabaseErrorCode::unbound_record_key,
            .detail = "Record argument key_1f0 is unbound",
        });
    }

    // Native 0x9a9367: MOV ECX, [EDX + 0x1f0] (captured once after initial
    // guard)
    const std::uint32_t target_key = *record->key_1f0;

    std::uint32_t cursor = base;
    std::size_t visited = 0U;

    while (cursor < end) {
        if (visited >= row_budget) {
            return std::unexpected(VmfNotificationDatabaseError{
                .code = VmfNotificationDatabaseErrorCode::resource_limit,
                .detail = "Row budget exhausted during 9A9340 group lookup",
                .row = visited,
                .cursor = cursor,
            });
        }

        const std::uint32_t offset_from_base = cursor - base;
        if ((offset_from_base % kGroupStride) != 0U) {
            return std::unexpected(VmfNotificationDatabaseError{
                .code = VmfNotificationDatabaseErrorCode::misaligned_cursor,
                .detail = "Group cursor is not aligned to 98h stride",
                .row = visited,
                .cursor = cursor,
            });
        }

        const std::size_t row_index =
            static_cast<std::size_t>(offset_from_base / kGroupStride);

        if (!group_table->rows.has_value()) {
            return std::unexpected(VmfNotificationDatabaseError{
                .code = VmfNotificationDatabaseErrorCode::table_unbound,
                .detail = "Group table rows span is unbound at reached row",
                .row = row_index,
                .cursor = cursor,
            });
        }

        if (row_index >= group_table->rows->size()) {
            return std::unexpected(VmfNotificationDatabaseError{
                .code = VmfNotificationDatabaseErrorCode::row_out_of_bounds,
                .detail = "Group row index exceeds available span capacity",
                .row = row_index,
                .cursor = cursor,
            });
        }

        const auto& row = (*group_table->rows)[row_index];

        // Native 0x9a9370-0x9a942d: inspect 15 references at offsets 18h, 20h,
        // 28h..88h in order.
        for (std::size_t slot = 0U; slot < kGroupReferenceSlots; ++slot) {
            const auto& ref_opt = row.references_18_88[slot];
            if (!ref_opt.has_value()) {
                return std::unexpected(VmfNotificationDatabaseError{
                    .code = VmfNotificationDatabaseErrorCode::unbound_reference,
                    .detail = "Group row reference slot is unbound",
                    .row = row_index,
                    .slot = slot,
                    .cursor = cursor,
                });
            }

            if (*ref_opt == nullptr) {
                // In native x86, dereferencing NULL reference pointer causes
                // fatal fault.
                return std::unexpected(VmfNotificationDatabaseError{
                    .code = VmfNotificationDatabaseErrorCode::invalid_binding,
                    .detail = "Group row reference is NULL pointer (invalid "
                              "native dereference)",
                    .row = row_index,
                    .slot = slot,
                    .cursor = cursor,
                });
            }

            const auto* ref_record = *ref_opt;
            if (!ref_record->key_1f0.has_value()) {
                return std::unexpected(VmfNotificationDatabaseError{
                    .code =
                        VmfNotificationDatabaseErrorCode::unbound_record_key,
                    .detail = "Referenced record key_1f0 is unbound",
                    .row = row_index,
                    .slot = slot,
                    .cursor = cursor,
                });
            }

            if (*ref_record->key_1f0 == target_key) {
                // Native: first match returns group row immediately without
                // reading later slots/rows.
                return &row;
            }
        }

        cursor += kGroupStride;
        ++visited;
    }

    return nullptr;
}

} // namespace tdur::ui
