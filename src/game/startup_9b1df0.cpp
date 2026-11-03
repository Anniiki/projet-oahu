#include "tdur/game/startup_9b1df0.hpp"

namespace tdur::game::startup {
namespace {

// Shape shared by the 0x50, 0x4C and 0x50 sites: allocation, then the
// constructor only when the block is nonzero, else EAX = 0. The value
// returned by the constructor is the one stored. No early return here.
using Construct = OpaqueWord (Init9b1df0Ports::*)(OpaqueWord, std::uint32_t);

OpaqueWord allocate_then_construct(Init9b1df0Ports& calls, std::uint32_t size,
                                   Construct construct) {
    const OpaqueWord block = calls.call_6113c0(size);
    if (block == 0)
        return 0;
    return (calls.*construct)(block, 5U);
}

} // namespace

Init9b1df0Outcome initialize_9b1df0(StartupServiceBase& self,
                                    Fields9b1df0& fields,
                                    GameModeRuntime& owner,
                                    Startup9b1df0Context context) {
    Init9b1df0Ports& calls = context.calls;
    Object100FC80& object_100fc80 = context.globals_9b1df0.object_100fc80;

    // 0x9B1E00..0x9B1E07.
    self.owner_10 = &owner;
    self.flag_18 = 0;
    calls.call_606bc0(fields.field_1c, NativeIdentity{0xF0'4A50});

    // 0x9B1E11..0x9B1E3B.
    Object6107C0* const object_6107c0 = calls.call_6107c0(object_100fc80);
    fields.field_78 = 0x30'0000;
    const Allocation block =
        context.allocator.allocate_slot20(0x30'0000U, 0U, 0U, 0U);
    fields.field_7c = block; // No older block is released.
    if (object_6107c0 == nullptr)
        return Init9b1df0Outcome::refused_null_6107c0;
    object_6107c0->field_04 = 5;

    // 0x9B1E42..0x9B1E60: live reads after the allocator callback.
    const std::uint32_t field_78 = fields.field_78;
    calls.call_611300(fields.field_7c, field_78);
    calls.call_610e80(object_100fc80, context.globals.block_1699070.field_4c,
                      0U);

    // 0x9B1E6A..0x9B1E81: JZ on the whole DWORD.
    if (calls.call_610840(object_100fc80) != 0) {
        calls.call_610900(object_100fc80);
        return Init9b1df0Outcome::rejected_610840;
    }

    // 0x9B1E84..0x9B1E90: global 0x143F520 read here, copied twice.
    Object143F520* const object_143f520 = context.globals.global_143f520;
    object_100fc80.field_2c = object_143f520;
    context.globals_9b1df0.global_faa598 = object_143f520;

    // 0x9B1E95..0x9B1EBB.
    fields.field_8c =
        allocate_then_construct(calls, 0x50U, &Init9b1df0Ports::call_612430);
    calls.call_6120f0(fields.field_8c, NativeIdentity{0xF0'4A34});

    // 0x9B1EC0..0x9B1EFA.
    fields.field_90 =
        allocate_then_construct(calls, 0x4CU, &Init9b1df0Ports::call_612500);
    calls.call_6120f0(fields.field_90, NativeIdentity{0xF0'4A1C});
    {
        const OpaqueWord argument = fields.field_8c;
        calls.call_611f90(fields.field_90, argument);
    }

    // 0x9B1EFF..0x9B1F39.
    fields.field_94 =
        allocate_then_construct(calls, 0x50U, &Init9b1df0Ports::call_612430);
    calls.call_6120f0(fields.field_94, NativeIdentity{0xF0'4A04});
    {
        const OpaqueWord argument = fields.field_90;
        calls.call_611f90(fields.field_94, argument);
    }

    // 0x9B1F3E..0x9B1F79: EDI keeps the block across 0x610EA0.
    OpaqueWord constructed = 0;
    if (const OpaqueWord edi = calls.call_6113c0(0x14CU); edi != 0) {
        const Object610EA0* const object = calls.call_610ea0(object_100fc80);
        if (object == nullptr)
            return Init9b1df0Outcome::refused_null_610ea0;
        constructed = calls.call_6126b0(edi, 5U, object->field_04);
    }
    fields.field_98 = constructed;
    calls.call_6120f0(fields.field_98, NativeIdentity{0xF0'49E8});
    {
        const OpaqueWord argument = fields.field_94;
        calls.call_611f90(fields.field_98, argument);
    }

    // 0x9B1F90..0x9B1FA3: +0x98 is read before 0x610EA0 is called.
    const OpaqueWord argument_98 = fields.field_98;
    // The returned pointer itself is the receiver; null is not refused.
    Object610EA0* const receiver = calls.call_610ea0(object_100fc80);
    calls.call_611f90_object(receiver, argument_98);

    // 0x9B1FA8..0x9B2063.
    if (run_9b1df0_tail(fields.tail, context.tail) != TailOutcome::completed)
        return Init9b1df0Outcome::refused_null_1699104;
    return Init9b1df0Outcome::completed;
}

ModeInitOutcome Service9b1df0::initialize(GameModeRuntime& owner) {
    last_outcome = initialize_9b1df0(*this, fields, owner, context_);
    if (*last_outcome == Init9b1df0Outcome::completed)
        return ModeInitOutcome::initialized;
    if (*last_outcome == Init9b1df0Outcome::rejected_610840)
        return ModeInitOutcome::native_rejected;
    return ModeInitOutcome::modern_refusal;
}

} // namespace tdur::game::startup
