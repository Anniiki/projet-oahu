# Population: standalone reconstruction of `0090C760`

## Result and scope

`legacy_population_step` reconstructs the body of `0090C760`: call
counter, target adjustment, consumption of the shared RNG, cap tied to
ranking five, search for the maximum score and retirement request. The
dependencies that remain external are explicit typed services. No OpenTDU
code, hook, process access, global clock or commercial resource is
incorporated.

This routine has **no time argument**. Its call frequency remains a host
policy; the one thousand calls do not represent one thousand
milliseconds. The module is not wired to the game and demonstrates neither
corrected native NPCs, nor their execution at 60 Hz, nor an effective
removal.

Files: `include/tdur/game/legacy_population.hpp`,
`src/game/legacy_population.cpp` and `tests/legacy_population_test.cpp`.

## Provenance and limits of the evidence

The existing Rea exports of
`<private-evidence>/reprise-2026-10-30/rea/paradise-ai-time/` are
reused without a new session:

| Item | Evidence |
| --- | --- |
| `34-procedure_assembly.json` | `ev_0f418358e66e57d22a5c13036633e836ba7b613977b82f9f2bf0fc82909ffaac`; instructions and order of effects |
| `35-procedure_pseudo_code.json` | `ev_88791fc517fbaabe85f0e7f437ca40e797bc947cdf0bbe30981c18f418bfb2ef`; secondary reading track |
| `disk-comparisons.json`, entry `90C760` | `ev_2ae84503f55ae0a374fdd6c87831b8cd775fc9b74b813f7fd2593a1625711668`; contiguous body of 360 bytes, `0090C760..0090C8C7` |

The image initially analyzed is the Platinum/Paradise secondary,
SHA-256 `f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
The complete body is identical to the official PC MC 1.66 A disk image,
image SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
SHA-256 of the body in both files:
`bb30e205c0f3427b01ad888308f691a18f56b5b82290333e2899af767209d150`.
The private validation rechecks the files and this range by mapping the
PE sections. It does not prove an execution in official memory.

The calling boundary was established in a separate private study:
`90C760` is a distinct step after the world and other services of
`969E70`, under conditions that may be modified earlier in the same pass.
This component does not replace those conditions.

## ABI and ported data

The native entry receives its object in **EAX**, copies this pointer into
ESI and ends with `RET`. Its return register does not constitute an
established boolean. The C++ result is a new diagnostic.

| Native source | Established representation and usage |
| --- | --- |
| Object `+4`, `+8` | 32-bit counter and target; arithmetic modulo `2^32` |
| `0143F738`, `0143F73C` | Bound tested by equality; cap compared as signed |
| Word pointed to by `013A8458` | Shared RNG state, supplied by `uint32_t&` reference |
| Player `01214790`, virtual slot `+C` | Copy of four DWORDs before `90C410`; their geometric meaning remains open |
| Manager `013A70EC`, `+2444C`, `+2448C` | Array of sixteen pointers followed by a signed count |
| Agent `+21C`, `+22D8`, `+98`, `+22E4` | Vehicle presence, controller, threshold and request byte |
| Controller `+34`, `+C` | Floating-point score and raw mode; only mode two allows retirement |

These C++ structures are logical, with no binary layout compatibility.
Unknown fields are not replaced by business meanings.
`classify(fields, copied_words)` provides the `90C410` boundary, without
claiming to reconstruct its ranking chain. `request_retirement(agent, -1.0F,
"too many bots")` provides the `923A90` boundary; the retirement itself
remains external. No service is simulated by a default success.

## Preserved order

1. Increment the counter modulo 32 bits, then compare the **new** word
   to one thousand as unsigned. `FFFFFFFF` becomes zero and does not adjust
   the target.
2. When the adjustment is due, read the target, reset the counter to zero,
   then process in this order: target zero → one; target equal to the bound
   → decrement; otherwise consume a single RNG value.
3. The RNG uses `state * 7FF8A3ED + 2AA01D31`, modulo `2^32`.
   The write of the shared word precedes the **reread** of the target.
   An unsigned draw strictly below `7FFFFFFF` increments;
   `7FFFFFFF` and higher values decrement. An RNG reference aliasing the
   target or the counter respects this order.
4. If the player exists, copy its four words, call the ranking, then, for
   a result of exactly five, reread target and cap and write their
   **signed** minimum, including in case of equality.
5. Reread the manager binding, its count and the target. A count less than
   or equal to the signed target, or non-positive, terminates without
   walking any slot.
6. Walk the slots in order. Skip an absent vehicle, then an absent global
   player, reread per candidate. The first score strictly greater than zero
   and than all previous ones wins; a tie keeps the first. The threshold
   and the mode play no part in this selection.
7. Reread the count and the winning slot. The score must strictly exceed
   its threshold, then its reread controller must have mode two. A refusal
   terminates; no lower-ranked candidate is tried.
8. Write the request byte to one **before** the retirement callback, with
   the exact float `BF800000 = -1.0F` and the reason `too many bots`.
   An already non-zero byte does not forbid a new request.

The comparisons correspond to `COMISS/JBE` with invalid floating-point
exceptions masked: NaN does not win and a NaN threshold refuses; signed
zero does not win; `+inf` can win with priority to the first. The test
bench uses `/fp:precise`, without fast-math; the native FP exception flags
are not reproduced.

## Safety, callbacks and valid domain

The host serializes accesses and keeps the context objects alive during
the call. Callbacks may mutate the fields, replace player/manager or
reenter synchronously. Later reads see the new values; the local variables
and the budget of each invocation remain distinct. `shared_ptr` instances
pin the player until the end of the ranking, then the manager and the
chosen agent until the retirement returns. The destructors of these
objects must neither mutate this context nor reenter.

The safety differences are explicit:

- Absent manager, absent effectively read agent or absent effectively read
  controller: typed error, without invalid dereference or fictitious score.
- Positive count requiring more than sixteen slots: error before the walk.
  A non-positive count, or one already below the target, keeps its native
  exit without an early check of data that this branch does not use.
- The final rereads and the native fallback to slot zero are preserved.
  The fallback during the walk is impossible since the index and the local
  limit come from the same count. Without concurrent access or callback
  during the walk, the count cannot change before the final reread; no
  native memory race is made legitimate.

Mutations preceding an error remain applied. A C++ exception coming from a
service propagates without rollback, continuation or automatic retry; a
partially applied retirement is not replayed. This exception contract is
new and is not attributed to the binary.

## Standalone validation

MSVC `19.44.35229`, SDK `10.0.26100`, Release, `/std:c++latest /W4 /WX
/permissive- /utf-8 /EHsc /fp:precise`; no warning exemption.
The C++ latest mode is a compiler mode, not a complete C++26 guarantee.

The test bench verifies **15 278 checks** per architecture, x64 and Win32:
counter and RNG boundaries, five thousand successive calls, shared stream
between two objects, real storage aliases, signed minimum, order of
services, DWORD copy, binding replacement, NaN/infinities/ties, each of the
sixteen slots, refusal without second choice, late errors, exceptions,
reentrancy and release of objects after callback.

Private configuration and reports:
`<private-evidence>/reprise-2026-10-30/legacy-population-cpp/`.
`validation.json` contains commands, versions, outputs, source and
executable fingerprints, as well as the observed PE architectures. The two
binaries are standalone witnesses; no game was launched for this delivery.

After integrating the module and its test into the main CMake, the **32
Release/x64 CTests pass**. An independent review found no blocking defect
in the arithmetic, the callback order or the lifetimes.
