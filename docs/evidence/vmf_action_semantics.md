# VMF actions: native contracts for the C++ executor

## Scope and status

Rea batch `rea/vmf-action-interpreter/session/`, secondary PC SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
The AddressSets and the assembly listings take precedence over pseudocode,
which sometimes loses SSE computations and register parameters. No commercial
listing is incorporated into the sources. The [usages across the 39 films](vmf_script_usage.md)
give the concrete consumers and distinguish abstract models from execution.

The [official trial 25](official_trial_25.md) corroborates 24 bodies and six
constants. This scope is exact: the helpers and wrappers below that do not
appear in it remain secondary. The [C++ stack](vmf_stack.md) and a
[19-opcode executor](vmf_execution.md) are now ported and tested.
The scalar-only entry point stops in front of the 17 host actions.

The [timeline hookup](vmf_timeline_actions.md) now adds five actions to the
entry point that receives a runtime and an explicit target; the 12 other host
boundaries and the complete player remain to be implemented.

## Values and storage

The native stack uses eight-byte slots in a space shared with the texts. The
observed types are `1` float32 number, `2` linked string, `3` sprite identity,
`4` string owned in that storage. Native pointers must never become
integers/addresses reused by the x64 client. The native type 5 variable also
yields a linked type 2 slot when it is read.

| Routine | Established contract |
| --- | --- |
| `44B2E0/103` | Text push: copies at most 31 bytes, terminates at 31, type 4; silently refuses when the computed storage boundary is reached. |
| `44B350/43` | Link push: type 2, keeps the supplied pointer; no 31-byte copy. |
| `44B200/68` | Numeric pop: +0 if the stack is empty; type 1 read as float, other data converted from text by 442530, then the slot is removed. |
| `44B250/104` | Text pop: types 2/4 give the text, otherwise formatting by 442330 with precision 12, capped at 4 in this helper. Reading the top precedes its count test: no safe fallback value is established on an empty stack. |
| `444600` | Simple pop: does nothing if empty; type 4 also restores the text cursor before removal. |
| `443620` | Variable read: type 4 copied again by 44B2E0; types 2/5 given by 44B350; any other field passed on as float32. |

The 31-byte copy does not apply to the direct SetTarget/GotoLabel operands.
The inventory finds four Push32, all consumed by Trace, and 50 direct
SetTarget longer than 31 bytes. A new owning storage must preserve the type
and the live link without keeping a view onto a temporary buffer.
The silent out-of-space behaviour and the unbounded reads of the native code
are not acceptable guards to copy into an executor of external data.

## Computations and branches

Arithmetic outputs are type 1; booleans are **texts "1" or "0"** pushed by
44B2E0. Binary operations first remove the top `b`, then `a`; missing numeric
values give +0. The SSE comparisons and their NaN handling must be preserved
explicitly.

| Opcode / body | Effect to reconstruct |
| --- | --- |
| `0A/444630`, `0B/444750`, `0C/444870` | Float32 `a+b`, `a−b`, `a*b`, without FMA. |
| `0D/444990` | Before `a/b`, replace `b` with the raw threshold `33D6BF95` if `threshold >= b`, including for negatives. The COMISS branch keeps a NaN denominator. |
| `0E/444AC0`, `0F/444BE0` | Ordered numeric equality; comparison `a<b` as ordered `b>a`. |
| `10/444CF0`, `11/444E50`, `12/444FB0` | And/Or/Not after numeric conversion; both binary operands are removed. Signed zero is false, NaN is non-zero for this logic. |
| `18/445430` | CVTTSS2SI32 then CVTSI2SS: truncation, not rounding. NaN/overflow follow the SSE integer indefinite value; an unbounded C++ cast does not reproduce it. |
| `99/445560`, `9D/4455B0` | Advance then signed 16-bit delta in the compact space; If removes the condition and branches if it is numerically non-zero. |

Conversion 442530 and its constants are described separately. For branches,
the C++ reader already supplies an exact target within the same stream.
This guard is stricter than the native rebase 443EF0, which can stop after an
overrun boundary. An instruction/call budget remains to be defined; the 1487
backward edges of the corpus do not establish real termination.

## Texts, variables and host boundaries

- StringEquals 445080 compares bytes up to NUL; StringAdd 4451B0
  composes `a` then `b` in a 32-byte buffer, result limited to 31.
  Texts obtained through 44B250 can however designate its same global
  formatting buffer; two successive numeric conversions can alias each
  other. It remains to be established which real consumers depend on this
  effect before freezing the value snapshots of the new runtime.
- StringLength 445130 counts **bytes**, not Unicode characters.
  StringExtract 445260 takes length, index, text; a positive index is
  decreased by one, a non-positive index starts at 0. Out-of-region values or
  negative lengths are not safe in the native code and require a guard in the port.
- GetVariable 445A60 can produce a type 3 sprite when the path does not
  designate a variable. A failure pushes an empty text; the resolution details
  and the lifetime remain separate contracts.
- SetVariable 445BA0 distinguishes number, type 2 link and copied text. It
  reads the value before the name. The setters 449240/449290/4492E0 resolve
  the instance and mark its flag `+6C|200h`; they are not corroborated by trial 25.
- The dictionaries 4435D0/4434B0 compare with `_stricmp` and instance ID.
  The hash 443390 receives that same identity as its seed: four-byte words
  masked with `DFDFDFDF`, addition modulo 32 then modulo capacity. The last
  partial word keeps the upper bytes of the previous word. This is not a
  Unicode casefold, nor a new hash to invent.
- Direct FSCommand 445CB0 and constructed FSCommand 445D40 recognise the exact
  ten-byte prefix then pass the suffix to the callback `context+9C8`, userdata
  `+9CC`. The second removes target then URL; its visible code does not read
  the flags. Only the first body is corroborated by trial 25.

An FSCommand can modify a variable that the script reads back immediately.
The future host must therefore respect ordering, reentrancy, ownership and
restoration of the context. A missing callback must not be replaced by a
fictitious success. The 10 Call 445640 and the Get/SetProperty still require
their complete port. The [formatter 442330](vmf_number_format.md) is ported
and hooked up to the shared text pop; computed names such as `line1` still
await the dictionary and the resolution of variables from the host context.

## Triggering, focus and open limits

443A60 executes the pointers in the action queue then resets its count to 0;
this does not establish a stack clear. 44B3F0 executes a condition when its
flags intersect the event mask. 44B430 compares the non-zero `u16flags>>9`
with the context code `+5C`, executes, then consumes that code by setting it
to 0. Their callers found are 448E60, 44FDF0 and 4473B0 respectively.
This does not map any physical Xbox button.

Trace 445920 removes its argument only if the global flag and the required
logger are present; otherwise it does nothing. The initialiser 44B1A0 resets
the stack count to 0 and prepares its cursors; the caller 4418B0 is
identified. Its cadence, the other resets and the real state of the logger
remain to be established. Do not replace these unknowns with the SWF4 model
"empty entry / Trace pop / maximum 6".

Key evidence of the batch: assembly listings 10..13,20..34,58..60,96/98/99/108..110;
resolutions 61..77 and 87/88/94; pseudocode 35..57,80..85,89..93,95/97/105..107.
Examples: hash `ev_f77c4d11ee3f18f8709e937c3b257acac92afbbf9af3081abe66fe5f1bb564c5`,
consumed key `ev_3c743ed923dd87473bd72b816c42b311097ae8da47287bb9f1c6e2924cbcfd36`.

## Recovered link to the zoom requests

Searches for the texts `zoin`/`zoou`, then for the bytes in that order, are
negative in the blocks examined. Searching for the four reversed bytes
finds five comparison sites by command: the handlers build an integer from
the first four characters in big-endian order. The immediate constants of
the x86 code are therefore stored in the other order. This result does not
authorise treating every occurrence as a complete handler.

Secondary paths established by pseudocode 123..131:

- `6BD120` recognises these commands, among others, in a branch guarded by
  `this+128`. Its acceptance does not mean that a zoom was performed.
- `75F340` uses this common route before calling `741A50` for zoin or
  `7419C0` for zoou; `9C1E60` can on the contrary return after this common
  route, then uses `9C0FA0`/`9C0E70` when it does not consume the command.
- Both families of effects go through `8DDC00`/`8DDCC0` and then republish
  WhatZoom through `8DC790`. This explains the importance of reading variables
  back after a callback in the scripts, without proving that this path ran in game.

Resolutions 132/134 bound `8DDC00/158` and `8DDCC0/157`. A static PE
comparison finds these **315 bytes identical in both files**, private
evidence `vmf-core-runtime-25/map-zoom-file-comparison.json`. They were not
read in the process of trial 25. Pseudocode 133/135 shows:

| Body | When the byte `+250` equals 0 |
| --- | --- |
| `8DDC00` | States `+240` 1/2/3/4: call `8DD0C0` with 2/3/4/5; otherwise return 0. |
| `8DDCC0` | States `+240` 2/3/4/5: call `8DD0C0` with 1/2/3/4; otherwise return 0. |

A non-zero byte gives return 0. The accepted branches also call 8DBD70
with one of two global values depending on the fields `+244`/`+240` read
back after the request. Both bodies wrap the processing with 606B60/606B90.
The actual mutation performed by 8DD0C0, the exact role of the fields, these
global values, the animation duration and the pan remain open; no callee or
physical control is promoted by this comparison. Two other token sites,
`78BBB2` and `7B1E32`, remain outside any identified Ghidra procedure.
