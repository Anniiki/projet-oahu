#include "tdur/mod/finish_band.hpp"

#include <bit>
#include <cstddef>

#if !defined(_M_IX86)
#error "The finish-band stub is x86 (Win32) machine code"
#endif

using tdur::mod::detail::FinishBandBinding;
using tdur::mod::detail::FinishBandCounters;

// Global names: the inline assembler addresses them directly.
static FinishBandBinding tdur_finish_band_binding{};
static FinishBandCounters tdur_finish_band_counters{
    tdur::mod::detail::finish_band_magic};

// Offsets used by the assembly below.
static_assert(offsetof(FinishBandBinding, callee) == 0x00);
static_assert(offsetof(FinishBandBinding, local_car_slot) == 0x04);
static_assert(offsetof(FinishBandCounters, calls) == 0x04);
static_assert(offsetof(FinishBandCounters, restored) == 0x08);
static_assert(offsetof(FinishBandCounters, not_local) == 0x0c);
static_assert(offsetof(FinishBandCounters, null_ai_vehicle) == 0x10);
static_assert(offsetof(FinishBandCounters, null_car) == 0x14);
static_assert(offsetof(FinishBandCounters, null_physics) == 0x18);
static_assert(offsetof(FinishBandCounters, refused_value) == 0x1c);
static_assert(offsetof(FinishBandCounters, last_before_bits) == 0x20);
static_assert(offsetof(FinishBandCounters, last_after_bits) == 0x24);

// Reached by CALL from the rewritten site; see finish_band.hpp for the
// contract. Stack after the four saves: [esp] EDX, [esp+4] ECX, [esp+8] EAX,
// [esp+0Ch] EFLAGS, [esp+10h] EDI copy, [esp+14h] return, then a1 and a2.
// clang-format off
extern "C" __declspec(naked) void TdurFinishBandStub() {
    __asm {
        push edi                         ; copy of the AI vehicle
        push dword ptr [esp + 0Ch]       ; a2 (address computed before push)
        push dword ptr [esp + 0Ch]       ; a1
        call dword ptr [tdur_finish_band_binding + 0] ; callee, RET 8
        pushfd                           ; the callee's flags
        push eax
        push ecx
        push edx
        inc dword ptr [tdur_finish_band_counters + 04h]
        mov eax, dword ptr [esp + 10h]   ; AI vehicle
        test eax, eax
        jz null_ai_vehicle
        mov eax, dword ptr [eax + 21Ch]  ; car
        test eax, eax
        jz null_car                      ; never compare null with null
        mov ecx, dword ptr [tdur_finish_band_binding + 4]
        cmp eax, dword ptr [ecx]         ; local player's car?
        jne not_local
        mov eax, dword ptr [eax + 39B4h] ; physics vehicle
        test eax, eax
        jz null_physics
        xor ecx, ecx                     ; band = 0.0
        cmp byte ptr [eax + 0D35h], 0
        jne bike_rule                    ; marked: keep 0.0
        mov ecx, dword ptr [eax + 3FCh]  ; band = configuration value
    bike_rule:
        cmp dword ptr [eax + 40F0h], 1   ; motorcycle?
        jne bounded
        mov ecx, 3F800000h               ; band = 1.0
    bounded:
        cmp ecx, 3F800000h               ; unsigned: +0.0 .. 1.0 only
        ja refused_value
        mov edx, dword ptr [eax + 41F0h]
        mov dword ptr [tdur_finish_band_counters + 20h], edx
        mov dword ptr [eax + 41F0h], ecx ; restore the installation band
        mov dword ptr [tdur_finish_band_counters + 24h], ecx
        inc dword ptr [tdur_finish_band_counters + 08h]
        jmp done
    refused_value:
        inc dword ptr [tdur_finish_band_counters + 1Ch]
        jmp done
    null_ai_vehicle:
        inc dword ptr [tdur_finish_band_counters + 10h]
        jmp done
    null_car:
        inc dword ptr [tdur_finish_band_counters + 14h]
        jmp done
    not_local:
        inc dword ptr [tdur_finish_band_counters + 0Ch]
        jmp done
    null_physics:
        inc dword ptr [tdur_finish_band_counters + 18h]
    done:
        pop edx
        pop ecx
        pop eax
        popfd                            ; flags as the callee left them
        lea esp, [esp + 4]               ; drop the EDI copy, flags intact
        ret 8                            ; the caller's a1 and a2
    }
}
// clang-format on

namespace tdur::mod::detail {

std::uintptr_t FinishBandStubAddress() noexcept {
    return reinterpret_cast<std::uintptr_t>(&TdurFinishBandStub);
}

FinishBandCounters& FinishBandCountersBlock() noexcept {
    return tdur_finish_band_counters;
}

FinishBandBinding ExchangeFinishBandBinding(FinishBandBinding next) noexcept {
    const auto previous = tdur_finish_band_binding;
    tdur_finish_band_binding = next;
    return previous;
}

} // namespace tdur::mod::detail
