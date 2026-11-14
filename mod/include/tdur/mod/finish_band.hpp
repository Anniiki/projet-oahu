#pragma once

#include "tdur/mod/code_patch.hpp"

#include <windows.h>

#include <array>
#include <cstdint>
#include <span>

namespace tdur::mod::detail {

// Diagnostic counters updated by the compiled finish-band stub. Unlocked
// increments: a wrong count never changes the gearbox band.
struct FinishBandCounters {
    std::uint32_t magic;            // +00 'FBND' = 0x444E4246
    std::uint32_t calls;            // +04
    std::uint32_t restored;         // +08
    std::uint32_t not_local;        // +0C
    std::uint32_t null_ai_vehicle;  // +10
    std::uint32_t null_car;         // +14
    std::uint32_t null_physics;     // +18
    std::uint32_t refused_value;    // +1C
    std::uint32_t last_before_bits; // +20 band left by the callee
    std::uint32_t last_after_bits;  // +24 band restored
};
static_assert(sizeof(FinishBandCounters) == 0x28);
inline constexpr std::uint32_t finish_band_magic = 0x444E4246;

// What the stub calls and compares. callee: original function, which pops
// its two stack arguments (RET 8). local_car_slot: address of the DWORD that
// holds the local player's car.
struct FinishBandBinding {
    std::uintptr_t callee{};
    std::uintptr_t local_car_slot{};
};

// Entry of the stub, compiled into this module: the target of the rewritten
// CALL. Contract, on entry: [esp] return address, [esp+4] a1, [esp+8] a2,
// ECX, EDX and EDI as the caller left them (EDI: AI vehicle).
// - ECX, EDX, EDI, EBX, ESI, EBP untouched before the call; a2 then a1 are
//   pushed again, so the callee sees the same stack arguments;
// - after the call, if EDI was not null, [EDI+0x21C] (car) is not null and
//   equals [local_car_slot], and [car+0x39B4] (physics) is not null, the
//   band [physics+0x41F0] takes the installation value: [physics+0x3FC] when
//   the byte [physics+0xD35] is zero, else 0.0; 1.0 when the DWORD
//   [physics+0x40F0] equals 1. Bits above 0x3F800000 (negative, NaN, > 1)
//   are never written;
// - every general register, EFLAGS, x87 and XMM register leaves as the
//   callee left it; RET 8 pops the caller's two arguments.
std::uintptr_t FinishBandStubAddress() noexcept;
FinishBandCounters& FinishBandCountersBlock() noexcept;

// Plain exchange, for the transaction owner and the source-owned fixture.
// Never call it while a detour to the stub may be executed.
FinishBandBinding ExchangeFinishBandBinding(FinishBandBinding) noexcept;

// One call site rewritten from CALL callee to CALL stub, with up to three
// exact signatures. names[i] labels signatures[i] in refusal events. The
// call site, every signature and the local car slot must be committed
// memory of region_type (MEM_IMAGE for the game) inside the allocation that
// starts at allocation_base (the main module for the game).
struct FinishBandSite {
    std::uintptr_t call_site{};
    std::uintptr_t callee{};
    std::uintptr_t local_car_slot{};
    std::array<CodeSignature, 3> signatures{};
    std::array<const char*, 3> names{};
    std::size_t signature_count{};
    const void* allocation_base{};
    DWORD region_type{};
};

using FinishBandLog = void (*)(const char* event, DWORD error,
                               const char* reason) noexcept;

// Named checks, then the binding, then one transaction (ApplyCodePatches).
// A null log is refused with E_POINTER before anything is read or written.
// Otherwise every call logs exactly one event:
// retail-finish-band-site-invalid, retail-finish-band-region-refused
// (reason: call-site, local-car-slot or the signature name),
// retail-finish-band-signature-refused (reason: signature name),
// retail-finish-band-target-refused, retail-finish-band-transaction-failed
// (reason: startup-refused-state-restored or
// startup-refused-private-state-incomplete), retail-finish-band-applied
// (reason: detour, stub and counters addresses). A refusal before the
// transaction, or a failed transaction whose state is restored, leaves the
// code and the previous binding unchanged; an incomplete state terminates
// the process inside ApplyCodePatches. The caller guarantees a single
// successful call per process.
HRESULT ApplyFinishBandFix(const FinishBandSite& site, FinishBandLog log,
                           CodePatchOperations operations = {}) noexcept;

} // namespace tdur::mod::detail
