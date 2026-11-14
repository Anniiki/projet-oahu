# Shift band after the finish line: fix for the official client

## Defect addressed

After an event, some of the player's cars shift gears at lower rpm. At the
finish line, the call at `0095A679` (to `0090C460`) applies an AI
personality to the player's car, and the shift band `[physics+41F0]` then
stays at 0.3 instead of its installation value. This is game logic only (AI
and gearbox).

Primary reference PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`; the
bytes below are identical in the Platinum executable
(`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`).

## Fix

The Win32 C++latest component [retail_finish_band.cpp](../../mod/src/retail_finish_band.cpp)
replaces in memory the five bytes of the call at `0095A679` with a call to a
function of the module, [TdurFinishBandStub](../../mod/src/finish_band_stub.cpp),
compiled by MSVC (naked function, inline assembly). No machine code is
generated at run time. The function:

- calls the original callee back through a pointer
  (`ExchangeFinishBandBinding`), with the same stack arguments, ECX, EDX and
  EDI;
- after the call, if the car `[EDI+21C]` is non-null and equal to
  `[01214790]` (the game's own criterion at `009CDA08`) and if the physics
  vehicle `[car+39B4]` is non-null, restores the band to its installation
  value: `[physics+3FC]` if the byte `[physics+D35]` is zero, otherwise 0.0;
  1.0 if the DWORD `[physics+40F0]` equals 1 (motorcycle). These fields are
  the ones that the installation routine at `00832ADB` rereads for this
  vehicle; no 0.7 constant appears in the code;
- never writes a value whose bits exceed `3F800000` (negative values, -0,
  NaN, infinities, anything above 1);
- returns EAX, ECX, EDX, EDI, EFLAGS, x87 and XMM as the callee left them,
  preserves EBX, ESI and EBP and ends with `ret 8`.

Opponents keep exactly the value of their personality. Diagnostic counters
(calls, restorations, refusals, last values) are exposed to the log.

## Signatures and refusals

| Name | Address | Bytes |
| --- | --- | --- |
| `site` | `0095A66F` (15) | `8b7bfc6a025033d233c9e8e21dfbff` |
| `callee` | `0090C601` (56) | `8b871c02000085c00f84910000008b80b439000085c00f8483000000f30f2a86cc000000f30f59056816f400f30f1180f04100005ec20800` |
| `constant` | `00F41668` (4) | `cdcccc3d` |

The bytes were reread in both executables, outside any protected area. The
target of the reread call must equal `0095A67E + FFFB1DE2 = 0090C460`.

Before the transaction, [finish_band_core.cpp](../../mod/src/finish_band_core.cpp)
checks, in this order, and logs exactly one event:

1. the site, the slot `01214790` and the three signatures lie in the main
   image (`MEM_IMAGE`, `AllocationBase` = main module): otherwise
   `retail-finish-band-region-refused`;
2. each signature: otherwise `retail-finish-band-signature-refused`, reason
   `site`, `callee` or `constant`;
3. the `e8` target: otherwise `retail-finish-band-target-refused`;
4. then a single `ApplyCodePatches` transaction (threads suspended,
   signatures rechecked, atomic write by CAS64 because the window
   `0095A679`-`0095A67D` fits in the word at `0095A678`): failure
   `retail-finish-band-transaction-failed`, reason
   `startup-refused-state-restored`; success `retail-finish-band-applied`,
   reason `detour=0x0095A679;stub=…;counters=…`.

A missing log is refused (`E_POINTER`) without reading or writing anything.
Any refusal leaves the game with its original behavior; the mod makes no
further attempt. Admission follows that of the background bridge
([retail_background_policy.md](retail_background_policy.md)): audited
identity, base `00400000`, first initialization, scope of the renderer
initializer (`retail-finish-band-phase-not-admitted` otherwise, once).
Closing that scope seals the fix (`retail-finish-band-sealed` or
`retail-finish-band-not-applied`).

## Option

`TDUR_RETAIL_FINISH_BAND`, read at load time under `TDUR_D3D12_DISPLAY=1`:
absent or `1`, active; `0`, off (`retail-finish-band-disabled`); any other
value refuses the load (`retail-finish-band-invalid-selection`). A launcher
that starts the game should remove any inherited value before setting it.

## Tests outside the game

| CTest | Content |
| --- | --- |
| `finish_band_stub_contract` | the real compiled stub, linked to a fake callee, behind a synthetic site detoured by `ApplyCodePatches`; compared with the original site: local car (0.7), motorcycle (1.0), flagged vehicle (0.0), bounds +0 and 1.0, four refused values, opponent, null physics vehicle, null car and slot, null AI vehicle, non-zero ECX and EDX, EDI destroyed by the callee; registers, flags, stack, counters |
| `finish_band_core_site` | `ApplyFinishBandFix` on a synthetic image: null log, invalid site, regions (type, allocation, slot, signature), wrong byte in each signature, wrong target, transaction refused and restored, success and second call |
| `display_injected_deferred_startup`, `display_finish_band_off`, `display_finish_band_invalid` | module injected into a witness: phase refusal logged by default, switch-off logged, load refused for an invalid value |

Mutants killed by these tests: EBX overwritten, local player test inverted,
motorcycle rule ignored, flags not restored, EDI taken after the call,
signed bound, wrong signature accepted by the core, null log tolerated,
region checks removed.

## To observe in game

- behavior against the real `0090C460` and identity of `[01214790]`
  (deduced, criterion reread at `009CDA08`);
- criteria fixed before the measurement: car 0.700 (`3F333333`) at the
  start, at the finish and in free roam afterwards; motorcycle 1.0 in free
  roam, 0.7 in a race, 1.0 after the finish;
- known deviation: the band is restored as soon as the call returns, hence
  also during the AI-driven finish cinematic (0.7 instead of 0.3).
