#pragma once

#include "tdur/game/startup_services.hpp"
#include "tdur/input/native_input_selection.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

// Initialization driver 0x980AF0 (thiscall, one argument, RET 4 at 0x980DFF).
// Field names are neutral (native offset or address) unless a label was
// verified. Native addresses document provenance only; nothing here calls,
// computes or dereferences a native address. AL = 1 only means that this
// initialization reached its end; no device, world or coop state follows.
// See docs/evidence/startup_driver_980af0.md for proof and modern policies.
namespace tdur::game::startup {

// Constant 0xF416D0, read from the verified PE file: float 0.8F.
inline constexpr float constant_f416d0 = 0.8F;

// Counters of one description of registry 0xFA9D30, parallel to the existing
// NativeInputDescription. field_0c is the declared record count (+0x0C);
// field_14 is the signed DWORD +0x14 bounding the rank loop. The modern
// count model does not reproduce wrapping x86 base/end addresses.
struct DescriptionCounters {
    std::uint32_t field_0c = 0;
    std::uint32_t field_14 = 0;
};

// Registry 0xFA9D30, shared with 0x980550. The three vectors are parallel
// views of the same slots; 0x433DF0 is the existing find_native_input_slot.
struct DescriptionRegistry {
    std::vector<std::optional<std::uint32_t>> ids;
    std::vector<std::shared_ptr<input::NativeInputDescription>> descriptions;
    std::vector<DescriptionCounters> counters;
};

// Borrowed block of the cell pool stored at row +0x18 (null = zero).
struct CellBlock {
    std::byte* data = nullptr;
    std::size_t bytes = 0;
    [[nodiscard]] bool is_null() const noexcept { return data == nullptr; }
};

// One 0x3C-byte row of the storage at [0xFA9DC0]. Only the written fields
// are typed; other bytes are kept as raw owned data.
struct CellRow {
    std::array<std::byte, 0x10> bytes_00{};
    std::uint32_t field_10 = 0;
    std::uint32_t field_14 = 0;
    CellBlock field_18;
    std::array<std::byte, 0x20> text_1c{}; // Receives the empty string.
};

// One 0x138-byte record of the array [0x11403D0]: only +0x130 (signed count)
// and +0x134 (table of row references) are read. A table whose data() is
// null stands for a null native table; its size is its owned capacity.
struct Record11403D0 {
    std::uint32_t field_130 = 0;
    std::span<CellRow*> table_134;
};

// Native pointer DWORD [0x1699134]: either an offset from null (offset 0 is
// the null pointer) or an offset into the pool's owned storage.
struct PoolCursor {
    bool in_storage = false;
    std::uint32_t offset = 0;
};

// Pool 0x1699130/34/38/3C. storage is the owned memory the cursor can reach.
// Its owner must outlive every borrowed CellBlock; an in_storage cursor
// requires nonnull backing, including a zero-length view.
struct CellPool {
    std::uint32_t dword_1699130 = 0; // Tested for zero only.
    PoolCursor cursor_1699134;
    std::uint32_t limit_1699138 = 0;
    std::uint32_t used_169913c = 0;
    std::span<std::byte> storage;
};

// Owned modern data standing for the globals of 0x980AF0. Callbacks may
// mutate it; the driver reads every value where the listing loads it.
struct Driver980AF0Globals {
    Object143F520* global_fa9d5c = nullptr;
    OpaqueWord global_fa9d98 = 0;
    OpaqueWord global_fa9d9c = 0;
    std::uint32_t global_fa9db0 = 0;
    std::uint32_t global_fa9da0 = 0; // Row capacity (signed compare).
    std::uint32_t global_fa9da4 = 0;
    std::uint32_t global_fa9dac = 0;
    Allocation global_fa9da8;
    std::uint32_t global_fa9d94 = 0; // Signed description pass count.
    std::uint32_t global_fa9dc4 = 0;
    std::uint32_t global_fa9dc8 = 0;
    std::uint32_t global_fa9db8 = 0; // Rows used (signed compare).
    std::span<CellRow> rows_fa9dc0;  // data() null = native null base.
    DescriptionRegistry registry_fa9d30;
    std::span<Record11403D0> records_11403d0; // Owned extent.
    std::uint32_t count_11403c4 = 0;          // Signed declared count.
    std::uint32_t value_11403cc = 0;
    CellPool pool;
    // Presence of the DWORD entry points; each is read just before its test.
    bool present_10e72b8 = false;
    bool present_10e72c4 = false;
    bool present_10e72c8 = false;
    bool present_10e72c0 = false;
    bool present_10e72d8 = false;
    bool present_10e72e0 = false;
};

// Frame buffer [ESP+0x20]: 0x400 bytes up to the end of the 0x410 frame.
using FrameBuffer = std::span<std::byte, 0x400>;

// Unresolved callees. Return values are typed only when consumed.
class Driver980AF0Ports {
  public:
    virtual ~Driver980AF0Ports() = default;
    // Register values at the call; their consumption is not demonstrated.
    virtual std::uint32_t call_4358d0(Object143F520* ecx_at_call,
                                      NativeIdentity esi_at_call,
                                      std::uint32_t ebx_at_call) = 0;
    virtual void call_433bc0(DescriptionRegistry& self) = 0;   // ECX 0xFA9D30.
    virtual void call_97fbd0(FrameBuffer buffer) = 0;          // EBX.
    virtual std::uint32_t call_97f860(FrameBuffer buffer) = 0; // EAX.
    virtual OpaqueWord call_via_9ec464() = 0;
    virtual std::uint32_t call_via_10e72b8(OpaqueWord arg1) = 0;
    // arg1 is the frame DWORD [ESP+0x10]; it is not read afterwards.
    virtual std::uint32_t call_via_10e72c4(std::uint32_t& local_10) = 0;
    virtual void call_via_10e72c8(std::uint32_t arg1) = 0;
    virtual void call_via_10e72c0(std::uint32_t arg1) = 0;
    virtual std::uint32_t call_via_10e72d8() = 0;
    virtual std::uint32_t call_via_10e72e0() = 0;
};

// Service-specific members of the 0x980AF0 object.
struct Fields980AF0 {
    std::uint8_t field_48 = 0;
    input::NativeInputSelection selection_58; // Written by 0x980550.
};

enum class Init980AF0Outcome {
    completed, // Native AL = 1.
    // Modern refusals: states the native would fault on or that exceed an
    // owned storage. Effects already performed are kept.
    refused_registry_views,
    refused_record_extent,
    refused_record_array_extent,
    refused_row_storage,
    refused_row_index,
    refused_cell_table,
    refused_null_row,
    refused_cell_block_address,
    refused_cell_block_extent,
    refused_selection_source,
    refused_selection_record_index
};

struct Driver980AF0Context {
    StartupGlobals& startup;
    Driver980AF0Globals& globals;
    Driver980AF0Ports& calls;
    AllocatorPort& allocator;             // Object 0xFA9CB0, slot +0x20.
    input::NativeInputNameLookup& lookup; // 0x434190 for 0x980550.
};

Init980AF0Outcome initialize_980af0(StartupServiceBase& self,
                                    Fields980AF0& fields,
                                    GameModeRuntime& owner,
                                    Driver980AF0Context context);

} // namespace tdur::game::startup
