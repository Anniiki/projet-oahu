#pragma once

#include "tdur/game/game_mode_lifecycle.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

// Initialization slots (+0x08) of the startup services listed by the
// controller, plus the two member helpers 0x9B40C0 and 0x9B4060.
// Names are neutral: a field is named after its native offset or address
// because its meaning is not established. Native addresses only document
// provenance; nothing here calls or dereferences a native address.
namespace tdur::game::startup {

// Identity of a native code or data address written as a value by a
// listing. It is never converted to a host pointer nor called.
struct NativeIdentity {
    std::uint32_t va = 0;
    friend bool operator==(NativeIdentity, NativeIdentity) = default;
};

// Value forwarded between opaque calls without interpretation. Widened so a
// modern implementation can carry an x64 value; never dereferenced here.
using OpaqueWord = std::uintptr_t;

// Borrowed result of allocator slot +0x20. The allocator/context owns its
// lifetime, including older blocks after reassignment. A null data pointer
// is the zero result; capacity bounds the writable storage of this view.
struct Allocation {
    std::byte* data = nullptr;
    std::size_t capacity = 0;
    [[nodiscard]] bool is_null() const noexcept { return data == nullptr; }
};

// Object 0xFA9CB0, slot +0x20, called with (size, 0, 0, 0). Its lifetime
// policy is the implementer's: assignments below never free older blocks.
class AllocatorPort {
  public:
    virtual ~AllocatorPort() = default;
    virtual Allocation allocate_slot20(std::uint32_t size, std::uint32_t arg2,
                                       std::uint32_t arg3,
                                       std::uint32_t arg4) = 0;
};

// Object referenced by global 0x143F520; only its byte +0x14 is written.
struct Object143F520 {
    std::uint8_t byte_14 = 0;
};

// Block at 0x1699070 (this of call 0x432FB0).
struct Block1699070 {
    Object143F520* field_2c = nullptr; // 0x169909C
    OpaqueWord field_4c = 0;           // 0x16990BC
    NativeIdentity field_64;           // 0x16990D4
    std::uint8_t field_72 = 0;         // 0x16990E2
    std::uint32_t field_7c = 0;        // 0x16990EC
    std::uint32_t field_80 = 0;        // 0x16990F0
    std::uint32_t field_84 = 0;        // 0x16990F4
};

// Block at 0x1699140 (this of call 0x438730).
struct Block1699140 {
    NativeIdentity field_2c;    // 0x169916C
    std::uint32_t field_3c = 0; // 0x169917C
    std::uint32_t field_40 = 0; // 0x1699180
    std::uint32_t field_44 = 0; // 0x1699184
    std::uint32_t field_4c = 0; // 0x169918C
    Allocation field_50;        // 0x1699190
};

// Sub-object at 0xF5B4E4 (this of call 0x6754A0); contents unknown.
struct Subobject5B4E4 {};

// Block at 0xF5B4B0 (this of call 0x6755A0).
struct BlockF5B4B0 {
    Object143F520* field_2c = nullptr; // 0xF5B4DC
    Subobject5B4E4 field_34;           // 0xF5B4E4
    Allocation field_38;               // 0xF5B4E8
    std::uint32_t field_3c = 0;        // 0xF5B4EC
};

// Sub-object at 0x169AA04 (this of both calls 0x41EE80).
struct Subobject169AA04 {
    Allocation field_00;        // 0x169AA04
    Allocation field_04;        // 0x169AA08
    std::uint32_t field_08 = 0; // 0x169AA0C
    std::uint32_t field_0c = 0; // 0x169AA10
};

// Addresses read or written by 0x9A7E10, named by their native address.
// Their grouping into a block is not demonstrated.
struct Region169A808 {
    std::uint32_t v169a808 = 0;
    std::uint32_t v169a80c = 0;
    Object143F520* v169a824 = nullptr;
    std::uint8_t v169a828 = 0;
    std::uint32_t v169a86c = 0;
    std::uint32_t v169a870 = 0;
    std::uint32_t v169a874 = 0;
    std::uint32_t v169a894 = 0;
    Allocation v169a898;
    std::uint32_t v169a89c = 0;
    std::uint32_t v169a8a0 = 0;
    NativeIdentity v169a8a8;
    NativeIdentity v169a8ac;
    std::uint32_t v169a8b8 = 0;
    std::uint32_t v169a8bc = 0;
    NativeIdentity v169a8c8;
    std::array<std::uint32_t, 64> v169a8dc{}; // 0x169A8DC..0x169A9DB.
    std::uint32_t v169a9f0 = 0;
    std::uint32_t v169a9f8 = 0;
    std::uint32_t v169a9fc = 0;
    std::uint32_t v169aa00 = 0;
    Subobject169AA04 v169aa04;
};

// Owned modern data standing for the globals read and written by the
// seven initializations. Callbacks may mutate it; every read is live.
struct StartupGlobals {
    Object143F520* global_143f520 = nullptr;
    Object143F520* global_fa9e24 = nullptr;
    Object143F520* global_fa8fd4 = nullptr;
    OpaqueWord global_10e8058 = 0;
    Block1699070 block_1699070;
    Block1699140 block_1699140;
    BlockF5B4B0 block_f5b4b0;
    Region169A808 region_169a808;
};

// Unresolved callees. Argument lists are the demonstrated pushes in order
// (first argument = last push). Return values are typed only when consumed.
class StartupCallPorts {
  public:
    virtual ~StartupCallPorts() = default;
    virtual void call_432fb0(Block1699070& self) = 0;
    // Indirect call through the DWORD at 0x9EC484.
    virtual OpaqueWord call_via_9ec484(OpaqueWord arg1, std::uint32_t arg2,
                                       std::uint32_t arg3, std::uint32_t arg4,
                                       std::uint32_t arg5,
                                       std::uint32_t arg6) = 0;
    // Indirect call through the DWORD at 0x9EC480.
    virtual void call_via_9ec480(OpaqueWord arg1, std::int32_t arg2,
                                 OpaqueWord arg3) = 0;
    // Indirect call through the DWORD at 0x9EC47C; arg2 is data 0xF08820.
    virtual void call_via_9ec47c(OpaqueWord arg1, NativeIdentity arg2) = 0;
    virtual void call_438730(Block1699140& self) = 0;
    virtual std::uint32_t call_6754a0(Subobject5B4E4& self) = 0;
    virtual void call_6755a0(BlockF5B4B0& self) = 0;
    // eax_at_call is the value the listing loads into EAX just before the
    // call; its consumption by 0x41EE80 is not demonstrated.
    virtual std::uint32_t call_41ee80(Subobject169AA04& self,
                                      std::uint32_t eax_at_call) = 0;
    // No argument is demonstrated (ECX is not set on every path).
    virtual std::uint32_t call_457e50() = 0;
};

// Common native members +0x04, +0x08, +0x10 and +0x18 of the services.
// Cycle slots +0x0C, +0x18 and +0x1C stay pure virtual: they are not
// reconstructed, and a concrete service must still supply them.
class StartupServiceBase : public ModeSubsystem {
  public:
    Allocation allocation_04;            // Written by 0x9B40C0.
    Allocation allocation_08;            // Written by 0x9B4060.
    GameModeRuntime* owner_10 = nullptr; // Initialization argument.
    std::uint8_t flag_18 = 0;
};

// Service-specific members.
struct Fields982C40 {
    float field_28 = 0.0F;
    float field_2c = 0.0F;
    float field_30 = 0.0F;
    float field_34 = 0.0F;
    float field_38 = 0.0F;
};

struct Fields9A7E10 {
    std::uint32_t field_1c = 0;
    std::uint8_t field_20 = 0;
};

// Every native body returns AL = 1. Only a modern refusal (state the
// native would fault on) differs; the result is never an authentication,
// connection or playability signal.
enum class InitOutcome { completed, refused_missing_object_143f520 };

struct StartupContext {
    StartupGlobals& globals;
    StartupCallPorts& calls;
    AllocatorPort& allocator;
};

// Table candidate 0xFA3FD0 (controller index 0).
InitOutcome initialize_98f020(StartupServiceBase& self, GameModeRuntime& owner,
                              StartupContext context);
// Table candidate 0xFA477C (controller index 2).
InitOutcome initialize_9cfa40(StartupServiceBase& self, GameModeRuntime& owner,
                              StartupContext context);
// Table candidate 0xFA3F90 (controller index 3). Constant 0xF4165C = 1.0F,
// read from the verified PE file.
InitOutcome initialize_982c40(StartupServiceBase& self, Fields982C40& fields,
                              GameModeRuntime& owner);
// Table candidate 0xFA4744 (controller index 6): no member write at all.
InitOutcome initialize_998dd0(StartupServiceBase& self, GameModeRuntime& owner);
// Table candidate 0xFA4720 (controller index 8).
InitOutcome initialize_9a7e10(StartupServiceBase& self, Fields9A7E10& fields,
                              GameModeRuntime& owner, StartupContext context);
// Table candidate 0xFA4760 (controller index 11).
InitOutcome initialize_999e80(StartupServiceBase& self, GameModeRuntime& owner,
                              StartupContext context);
// Table candidate 0xFA4068 (controller index 12).
InitOutcome initialize_9b3f80(StartupServiceBase& self, GameModeRuntime& owner);

// Global 0x10E7238 as read by the helpers.
struct HelperContext {
    std::uint8_t byte_e4 = 0;
    std::uint32_t dword_ec = 0;
};

// The helpers load the global twice; the second load may differ.
class HelperContextSlot {
  public:
    virtual ~HelperContextSlot() = default;
    virtual HelperContext* load() = 0;
};

enum class HelperOutcome {
    inactive,                    // Native: byte +0xE4 zero, no effect.
    filled,                      // Native: allocation stored, fill done.
    refused_missing_context,     // Modern refusal: null global context.
    refused_fill_exceeds_storage // Modern refusal: fill > owned capacity.
};

// 0x9B40C0: stores into member +0x04 and fills with 0xBD.
HelperOutcome initialization_helper_9b40c0(StartupServiceBase& member,
                                           HelperContextSlot& global,
                                           AllocatorPort& allocator);
// 0x9B4060: stores into member +0x08 and fills with 0xAD.
HelperOutcome initialization_helper_9b4060(StartupServiceBase& member,
                                           HelperContextSlot& global,
                                           AllocatorPort& allocator);

} // namespace tdur::game::startup
