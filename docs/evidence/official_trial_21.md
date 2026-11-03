# Official trial 21: code and physics context

## Execution and preservation

The official PC MC 1.66 A copy was verified and locked, in the private
isolated set
`<isolated-copies>/slot0/20261030T164158-32976/`.
PID **2692** and creation FILETIME **134378521269961313** agree across the
reports. The Job object closes the process after 120.171 seconds, at the
requested 120-second limit, code 1460. The slot is released; the 6,697
reference files and the 17 source-savegame fingerprints are unchanged. This
trial uses the temporary Win32 bridge, not the final x64 renderer.

## Code present

All **84 ranges / 18,503 bytes** are readable. The 17 additions since trial 20
account for 3,640 bytes, including seven material/variant ranges; the other
additions belong to studies outside this document.

The five ranges containing our adaptations have 18 identified bytes: physics
gate 5, graphics initialization 7, three branches of 2 each. Their other
bytes match both references. No global image equality and no execution of
the new bodies is inferred.

## Inputs and clocks

The 32 slots capture timer 16, x87 control word `027F`, MXCSR control `1F80`,
status zero once then `24` 31 times. The return address `74CA75F6` is
uniform; the logger classifies it in the module named **winmm** at
publication time. The system identity of that DLL, the thread at capture time
and a specific timeSetEvent call are not attested by this classification.
Module unloading followed by address reuse remains a limitation.
The ticks and TID in the text belong to the logger; the slots do not count
subsequent events.

The initial comparison of cell `9EC4A8` finds no equality with the winmm
API; winmmbase is not available at that moment. The cell is neither tracked
nor called, and this result is not carried over to the time of the captures.

26 series of 30 fields/96 bytes are readable over **5.261424 seconds**.
The integrator's **entry** counter goes from **3,352 to 3,830**, ED stays 0,
and the target is never in the foreground during this series. The unscaled
clock advances, but `timer_scale=0` and the scaled total stays `4.26801205`.
This reading does not prove a completed step, a non-zero delta, world
motion or an exact frequency. FPU and scalar values do not form a common
snapshot. The consumed AA/HDRI/LOD settings are 2/1/2, with no evidence of
their visual effect.

## Binding probe refusal

PID, creation time, file and lock all match the expected values, but the
probe returns **identity_refused** after 28 bytes of PE headers: **0/66
payload bytes**. No pass and no world value is available. Base `00400000`,
module size 29032448 and QPC 10000000 are reported. On disk, SizeOfImage is
also 29032448, with alignment 4096.

The PE check boolean, the user-address ceiling and the seven received DWORDs
are missing from the report. If the PE check is false, the seven completed
reads isolate its last comparison; the complete cause remains undemonstrated.
DetourRestoreAfterWith is called, but its return value is not attested. Its
native 32-bit path does not normally change SizeOfImage; no cause related to
the protection is attributed. A subsequent extension now exposes these
already-acquired values, without a new read or any relaxation of the guards:
`maximum_user_address`, `pe32_header_identity_verified`, `identity_sha_verified`
and up to seven RVA/value pairs under `identity_header_words`.
The values are retained at probe initialization, with no snapshot shared
across passes. The Win32 fixture passes 36 checks and the two targeted CTests
succeed. No new official trial has yet exercised this report.

These observations authorize no Havok call. Driving, fixed physics,
FPS-independent NPCs, the online session and co-op remain unvalidated.

## Private evidence

`evidence-manifest.json` pins ten reports/logs. Main fingerprints:

| Report | SHA-256 |
| --- | --- |
| `physics-context.json` | `2497f32701ba120a87e759b78238aa5c903e43ddb810e7f8eca7fac5ab4aea8e` |
| `physics-binding.json` | `d03376b237530b463573ef68e26566932df779cf0a801661893b47aee20a0408` |
| `code-helpers/comparisons.json` | `b4eee9b2aa55391936d6a93824ac8f7ed9dc359fd6909fd9650c2713e0de5678` |
| `timing-scalars.json` | `fe0806b13ce81b084c77a5312d799aa50f7ec47b1033bf9bf7cf685f1f2c1f8b` |

Independent reviews are retained in `rea/material-bindings/`. No raw
commercial byte or game resource is added to the repository.
