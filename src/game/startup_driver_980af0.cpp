#include "tdur/game/startup_driver_980af0.hpp"

#include "tdur/input/native_input_state.hpp"

#include <algorithm>
#include <cstring>

namespace tdur::game::startup {
namespace {

using Outcome = Init980AF0Outcome;

std::int32_t as_signed(std::uint32_t value) noexcept {
    return static_cast<std::int32_t>(value);
}

std::uint32_t record_dword_00(const input::NativeInputRecord& record) {
    std::uint32_t value = 0;
    std::memcpy(&value, record.bytes.data(), sizeof value);
    return value;
}

void store_float(input::NativeInputRecord& record, std::size_t offset,
                 float value) {
    std::memcpy(record.bytes.data() + offset, &value, sizeof value);
}

// 0x980BA0..0x980C17: one pass over description ID 2, ordinal pass.
Outcome stamp_description(Driver980AF0Globals& globals, std::uint32_t pass) {
    auto& registry = globals.registry_fa9d30;
    if (registry.ids.size() != registry.descriptions.size() ||
        registry.ids.size() != registry.counters.size())
        return Outcome::refused_registry_views;
    // 0x980BA8: existing 0x433DF0 reconstruction; 0x980BAF JZ on null.
    const auto slot = input::find_native_input_slot(registry.ids, 2, pass);
    if (!slot)
        return Outcome::completed;
    const auto& description = registry.descriptions[*slot];
    if (!description)
        return Outcome::refused_registry_views;
    const DescriptionCounters& counters = registry.counters[*slot];
    // 0x980BB6 JLE then 0x980C07 JL: signed rank against live +0x14.
    for (std::int32_t rank = 0; rank < as_signed(counters.field_14); ++rank) {
        // 0x980BC0..0x980BE5: first-to-last scan bounded by +0x0C, counting
        // records whose DWORD +0 is 1 (0x980BD3) until the rank-th one.
        auto& records = description->records;
        std::uint32_t matches = 0;
        for (std::uint32_t index = 0; index < counters.field_0c; ++index) {
            if (index >= records.size() || !records[index])
                return Outcome::refused_record_extent;
            input::NativeInputRecord& record = *records[index];
            if (record_dword_00(record) != 1u)
                continue;
            if (matches != static_cast<std::uint32_t>(rank)) {
                ++matches; // 0x980BDC.
                continue;
            }
            // 0x980BF9 and 0x980BFE; every other byte is preserved.
            store_float(record, 0x38, constant_f416d0);
            store_float(record, 0x3c, constant_f416d0);
            break;
        }
    }
    return Outcome::completed;
}

// 0x980CAC..0x980D0A: block of count * 8 bytes from the cell pool.
Outcome take_cell_block(CellPool& pool, std::uint32_t count, CellBlock& block) {
    block = {};
    const std::uint32_t size = count * 8u; // 0x980CB6, DWORD modulo 2^32.
    if (pool.dword_1699130 == 0)           // 0x980CBD JZ.
        return Outcome::completed;
    const std::uint32_t end = pool.used_169913c + size;
    if (end > pool.limit_1699138) // 0x980CCE JA, unsigned.
        return Outcome::completed;
    const PoolCursor start = pool.cursor_1699134; // 0x980CD0.
    pool.cursor_1699134.offset = start.offset + size;
    pool.used_169913c = end;
    if (!start.in_storage) {
        if (start.offset == 0) // 0x980CEC: null cursor, block zero.
            return Outcome::completed;
        return Outcome::refused_cell_block_address;
    }
    // An owned cursor needs an actual backing address, even for zero bytes.
    // A nonnull empty view is valid; the native null cursor is modeled above.
    if (pool.storage.data() == nullptr)
        return Outcome::refused_cell_block_address;
    // Owned extent of the entry loop (6 bytes per entry) and of REP STOS.
    const std::uint64_t entries =
        as_signed(count) > 0 ? std::uint64_t{count} * 8u - 2u : 0u;
    const std::uint64_t needed = std::max<std::uint64_t>(entries, size);
    if (start.offset > pool.storage.size() ||
        needed > pool.storage.size() - start.offset)
        return Outcome::refused_cell_block_extent;
    std::byte* data = pool.storage.data() + start.offset;
    // 0x980CF2 JLE (signed), then DWORD +0 and WORD +4 of each entry.
    if (as_signed(count) > 0) {
        for (std::uint32_t entry = 0; entry < count; ++entry)
            std::memset(data + std::size_t{entry} * 8u, 0, 6);
    }
    block = CellBlock{data, size};
    return Outcome::completed;
}

// 0x980C19..0x980D63: rows of 0x3C bytes and cells of 8 bytes.
Outcome fill_rows(Driver980AF0Globals& globals, std::uint32_t& local_10) {
    const std::uint32_t declared = globals.count_11403c4;
    if (as_signed(declared) <= 0) // 0x980C20 JLE.
        return Outcome::completed;
    for (std::uint32_t index = 0; index < declared; ++index) {
        if (index >= globals.records_11403d0.size())
            return Outcome::refused_record_array_extent;
        Record11403D0& record = globals.records_11403d0[index];
        const std::uint32_t count = record.field_130; // 0x980C40.
        const std::span<CellRow*> table = record.table_134;
        if (as_signed(count) <= 0) // 0x980C50 JLE.
            continue;
        local_10 = count; // 0x980C56.
        for (std::uint32_t slot = 0; slot < count; ++slot) {
            CellRow* row = nullptr;
            // 0x980C66 JL: signed rows used against capacity.
            if (as_signed(globals.global_fa9db8) <
                as_signed(globals.global_fa9da0)) {
                const std::uint32_t used = globals.global_fa9db8;
                globals.global_fa9db8 = used + 1u; // 0x980C78.
                // 0x980C90..0x980C98: empty string 0xF035DF at P+0x1C.
                if (globals.rows_fa9dc0.data() == nullptr)
                    return Outcome::refused_row_storage;
                if (as_signed(used) < 0 || used >= globals.rows_fa9dc0.size())
                    return Outcome::refused_row_index;
                row = &globals.rows_fa9dc0[used];
                row->text_1c[0] = std::byte{0};
            }
            // 0x980CA2: T[j] = P, before P+0x10.
            if (table.data() == nullptr || slot >= table.size())
                return Outcome::refused_cell_table;
            table[slot] = row;
            const std::uint32_t cells = globals.value_11403cc; // 0x980CA4.
            if (row == nullptr) // 0x980CA9 would write at P+0x10 with P = 0.
                return Outcome::refused_null_row;
            row->field_10 = cells;
            CellBlock block;
            const Outcome taken = take_cell_block(globals.pool, cells, block);
            if (taken != Outcome::completed)
                return taken;
            row->field_18 = block; // 0x980D0E; nothing is released.
            if (!block.is_null()) {
                // 0x980D13..0x980D29: REP STOS of [P+0x10] * 8 bytes.
                const std::uint32_t bytes = row->field_10 << 3;
                if (bytes > block.bytes)
                    return Outcome::refused_cell_block_extent;
                std::memset(block.data, 0, bytes);
                row->field_14 = 0;
            }
            --local_10; // 0x980D37.
        }
    }
    return Outcome::completed;
}

// 0x980D81..0x980DE7: +0x48 = 1 only at the end of the full chain.
void run_optional_chain(Fields980AF0& fields, Driver980AF0Globals& globals,
                        Driver980AF0Ports& calls, std::uint32_t& local_10) {
    const OpaqueWord value = calls.call_via_9ec464(); // 0x980D81.
    if (!globals.present_10e72b8)                     // 0x980D8F.
        return;
    if (calls.call_via_10e72b8(value) != 0) // 0x980D96.
        return;
    if (!globals.present_10e72c4) // 0x980D9F.
        return;
    if (calls.call_via_10e72c4(local_10) != 0) // 0x980DAA.
        return;
    if (globals.present_10e72c8) // 0x980DB3; result not consumed.
        calls.call_via_10e72c8(6);
    if (globals.present_10e72c0) // 0x980DC0; result not consumed.
        calls.call_via_10e72c0(0x3391);
    if (!globals.present_10e72d8) // 0x980DD0.
        return;
    if (calls.call_via_10e72d8() != 0) // 0x980DD6.
        return;
    if (!globals.present_10e72e0) // 0x980DDF.
        return;
    if (calls.call_via_10e72e0() != 0) // 0x980DE5.
        return;
    fields.field_48 = 1; // 0x980DE7.
}

} // namespace

Init980AF0Outcome initialize_980af0(StartupServiceBase& self,
                                    Fields980AF0& fields,
                                    GameModeRuntime& owner,
                                    Driver980AF0Context context) {
    auto& startup = context.startup;
    auto& globals = context.globals;
    // 0x980AFD and 0x980B09 load before the member writes.
    const OpaqueWord value_10e8058 = startup.global_10e8058;
    Object143F520* object = startup.global_143f520;
    self.owner_10 = &owner; // 0x980B0F.
    self.flag_18 = 0;       // 0x980B12.
    const OpaqueWord value_16990bc = startup.block_1699070.field_4c;
    globals.global_fa9d5c = object;
    globals.global_fa9d9c = value_10e8058;
    globals.global_fa9d98 = value_16990bc;
    globals.global_fa9db0 = 0;
    const std::uint32_t base =
        context.calls.call_4358d0(object, NativeIdentity{0xFA'9D8C}, 0);
    // 0x980B42..0x980B5A: DWORD arithmetic modulo 2^32, live reads.
    const std::uint32_t rows = globals.global_fa9da0 * 0x3Cu;
    const std::uint32_t cells = globals.global_fa9da4;
    const std::uint32_t size = base + cells * 8u + 0x10'0000u + rows;
    globals.global_fa9dac = size;
    const Allocation result = context.allocator.allocate_slot20(size, 0, 0, 0);
    globals.global_fa9da8 = result; // 0x980B75; older block not released.
    context.calls.call_433bc0(globals.registry_fa9d30); // 0x980B7A.
    // 0x980B7F reads 0xFA9D94 before the 0xFA9DC8 copy; JLE at 0x980B91.
    const bool stamp = as_signed(globals.global_fa9d94) > 0;
    globals.global_fa9dc8 = globals.global_fa9dc4;
    if (stamp) {
        std::int32_t pass = 0;
        do {
            const Outcome outcome =
                stamp_description(globals, static_cast<std::uint32_t>(pass));
            if (outcome != Outcome::completed)
                return outcome;
            ++pass; // 0x980C14, then signed JL against live 0xFA9D94.
        } while (pass < as_signed(globals.global_fa9d94));
    }
    // Frame DWORD [ESP+0x10]: n then decremented to 0 by each row loop. Its
    // native value is indeterminate when no row loop ran; modern value 0.
    std::uint32_t local_10 = 0;
    if (const Outcome outcome = fill_rows(globals, local_10);
        outcome != Outcome::completed)
        return outcome;
    // Frame buffer [ESP+0x20]: native content uninitialized; modern zeroed.
    std::array<std::byte, 0x400> buffer{};
    fields.field_48 = 0;                                     // 0x980D6D.
    context.calls.call_97fbd0(FrameBuffer{buffer});          // 0x980D71.
    if (context.calls.call_97f860(FrameBuffer{buffer}) == 0) // 0x980D7F.
        run_optional_chain(fields, globals, context.calls, local_10);
    // 0x980DEE: final selection by the existing 0x980550 reconstruction.
    auto& registry = globals.registry_fa9d30;
    const input::NativeInputDescriptionSlots source{registry.ids,
                                                    registry.descriptions};
    switch (input::select_native_input_commands(fields.selection_58, source,
                                                context.lookup)) {
    case input::NativeInputSelectionStatus::completed:
        break;
    case input::NativeInputSelectionStatus::invalid_source:
        return Outcome::refused_selection_source;
    case input::NativeInputSelectionStatus::invalid_record_index:
        return Outcome::refused_selection_record_index;
    }
    return Outcome::completed; // 0x980DF6: AL = 1.
}

} // namespace tdur::game::startup
