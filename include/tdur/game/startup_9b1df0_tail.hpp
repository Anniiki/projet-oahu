#pragma once

#include "tdur/game/startup_services.hpp"

#include <array>
#include <cstdint>

// Final sub-block 0x9B1FA8..0x9B2063 of initialization 0x9B1DF0 (controller
// index 9, candidate table 0xFA4320). Every path of this sub-block
// is established by the listing; the callees are opaque ports. This is not
// the complete initializer; its prefix and callees remain unimplemented.
namespace tdur::game::startup {

// Object whose pointer is the DWORD at 0x1699104; slot +0x20 is called with
// the pushes (0x18, 0, 0, 0). Its result is only compared with zero.
class AllocatorObject1699104 {
  public:
    virtual ~AllocatorObject1699104() = default;
    virtual OpaqueWord slot20(std::uint32_t arg1, std::uint32_t arg2,
                              std::uint32_t arg3, std::uint32_t arg4) = 0;
};

// Opaque callees. Bytes at 0x402370, 0x402320 and 0x401EA0 do not decode as
// ordinary code in the verified PE: their consumption of EDI and EAX and
// their preservation of EDI are NOT demonstrated. Each port therefore
// receives the register values loaded by the caller and returns the EDI
// value the native code stores after the call (an implementation that
// preserves EDI returns edi_at_call).
class Tail9b1df0Ports {
  public:
    virtual ~Tail9b1df0Ports() = default;
    // Live read of the DWORD at 0x1699104 (reloaded before each call).
    virtual AllocatorObject1699104* load_1699104() = 0;
    virtual OpaqueWord call_402370(OpaqueWord edi_at_call,
                                   std::uint32_t eax_at_call) = 0;
    virtual OpaqueWord call_402320(OpaqueWord edi_at_call,
                                   std::uint32_t eax_at_call) = 0;
    virtual OpaqueWord call_401ea0(OpaqueWord edi_at_call,
                                   std::uint32_t eax_at_call) = 0;
};

// Members of the service written by the sub-block, named by native offset.
struct Fields9b1df0Tail {
    OpaqueWord field_9c = 0;
    std::uint32_t field_a0 = 0;
    std::uint32_t field_a4 = 0;
    OpaqueWord field_a8 = 0;
    OpaqueWord field_ac = 0;
    std::uint32_t field_130 = 0;
    std::array<std::uint32_t, 0x90> field_134{}; // +0x134..+0x373
    std::array<std::uint32_t, 0x1F> field_374{}; // +0x374..+0x3EF
};

// completed: native AL = 1. refused_null_1699104: modern refusal where the
// native would dereference a null object; writes already made stay made.
enum class TailOutcome { completed, refused_null_1699104 };

TailOutcome run_9b1df0_tail(Fields9b1df0Tail& self, Tail9b1df0Ports& ports);

} // namespace tdur::game::startup
