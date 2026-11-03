# VMF: persistent targets, labels and script resumption

## Scope and provenance

This batch connects the SetTarget `8B`, TellTarget/SetTarget2 `20` and
GotoLabel `8C` contracts to a persistent context. It is neither a frame
player nor an executor of a script queue. The new evidence is static, taken
from the secondary Paradise PE, SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
No new official invocation or XEX equivalence is established.

The exports live in `rea/vmf-target-resolution/session/`, under the private
evidence of October 31, 2026. Earlier pseudocode serves as a map; the branches
below are reread in the assembly listings and the exported tables:

| Routine | Assembly / byte read | Body size |
| --- | --- | ---: |
| Path resolution `443FE0` | 09 / 10 | 169 |
| Child lookup `447FE0` | 11 / 12 | 132 |
| SetTarget `4443E0` | 13 / 14 | 74 |
| TellTarget `444430` | 15 / 16 | 95 |
| Jump after missing target `444090` | 17 / 18 | 124 |
| Label resolution `448990` | 19 / 20 | 170 |
| GotoLabel `444490` | 21 / 22 | 116 |

Reads 23/24 add the dispatch tables of `448990`: `448A3C`, 20 bytes, then
`448A50`, 44 bytes. The independent record
`vmf-label-resolution-review/validation-v2.json` keeps the SHAs, Evidence IDs,
parameters and the check of the five pointers and 44 discriminants, with no
new read of the PE by the reviewer.

## Origin, current target and child lookup

Write `G=[10E8078]`, `T=[G+58]` and Q for the action context. The interpreter
`443AF0` captures the origin into `Q+14` on entry; it does not recapture it
between two actions. Both target handlers always reset `G.current` to this
origin before resolving their operand. In `20`, this write precedes the pop
`44B250`: a safe port that refuses this pop therefore keeps the reset already
published. The stack remains shared; no new stack is created here.

`443FE0` treats the path as C-string bytes:

- An empty path, or one starting with `:`, succeeds without changing the
  origin.
- A leading `/` climbs the parent links up to the root, then processes the
  remainder. Multiple slashes are not normalized as a file path would be.
- The `..` prefix consumes exactly two bytes and takes the parent; it does
  not require the end of a component. Climbing above root produces a failure.
- Names are searched in the active list, in its depth order, with a non-null
  child and a virtual kind equal to zero. The cache is not traversed.

`447FE0` compares the **whole stored name** before checking the next
delimiter of the input: NUL, `:` or `/`. The operand must not be pre-split on
`/`: a stored name that literally contains this character can be selected.
The predicate is `(inputByte XOR nameByte) & 0xDF == 0`, not a locale-dependent
CRT comparison. It ignores bit `0x20` for every byte.

A missed lookup writes a null target. The modern handle, quota or unbounded
access errors remain distinct from this native absence. The DF mask can in
particular treat an input NUL as a space in the name and then advance past
the text: the port refuses this read with `invalid_native_state`, without
inventing padding. `resolve_target` is const and only publishes its result;
the execution context owns the resulting current target.

## Target failure and recovery scan

`444090`, evidence
`ev_fdb463e54c531d23d683e1cbfbd2c03fbcd0ff4c68fdb52c5030fcf2c7d9ded6`,
sets `Q.advance=0`, advances at least once from the failed action, then scans
linearly until End, `8B` or `20`. It keeps the IP on that recovery action.
The dispatcher suppresses the common advance once and then sets its flag back
to one. The skipped actions, including Jump/If/Push, are not executed.

The C++ continuation must preserve this phase between two work limits. It
replays neither the resolution that already failed nor the pop of the `20`.
A null target is accepted for this scan and its recovery; it triggers no root
fallback. The next `8B/20` starts again from the captured origin.

## Labels: comparison and frames

`448990`, evidence
`ev_968af76f8bc2d7eb2bcea7db6a2b9e3e12215cebcbf580f2b0140715ab1da1b4`,
receives T in ESI and the text in EBX. It saves current and the record
cursor, starts a temporary current at 1, then begins from `R+DWORD[R+14]`,
with `R=[T+18]`. The tables confirm End=0, ShowFrame=1, sprite=39 and
label=43. ShowFrame increments the frame; a label compares its text
`record+8`; the other records follow their links, skipping sprite definitions.

The jump of type 39 actually writes the cursor of `[G+58]`, not directly the
one of ESI. The two callers studied, GotoLabel and Call, set ESI=G.current
before this call. The native helper must therefore not be described as safe
for an arbitrary ESI timeline independent of G.current.

The first matching label returns its 1-based frame; End returns zero. Both
exits restore current and the cursor. The C++ lookup can use the already
validated `VmfLabel` entries, filtered by the **instance's own resource**, in
source order, with `frame_index+1`. It searches neither child sprites nor
anywhere else in the movie. A duplicate is not a refused ambiguity: the first
match wins. A live cached timeline can be queried without being reactivated.

The comparison calls `_strnicmp` with count=63. The secondary import is
`MSVCR71.dll`, IAT RVA `5EC258`, hence VA `9EC258` for base `400000`; proof
`paradise-game/01-inspect_pe_imports.json`, evidence
`ev_322f791f1bcd2fc418461e0e5a0727575714ee5ccff444d9c557b215e61f03f7`.
The predicate stops at NUL and ignores suffixes beyond the first 63 bytes.
The direct `8C` operand is not subject to the Push truncation at 31; nor does
it pass through a numeric conversion or a path resolver.

The [Microsoft documentation of `_strnicmp`](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/strnicmp-wcsnicmp-mbsnicmp-strnicmp-l-wcsnicmp-l-mbsnicmp-l?view=msvc-170)
describes a bounded comparison by lowercase conversion according to the
current locale. The [related documentation of `_stricmp`](https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/stricmp-wcsicmp-mbsicmp-stricmp-l-wcsicmp-l-mbsicmp-l?view=msvc-170)
specifies the dependency on `LC_CTYPE`. The port uses the CRT and the locale
of the calling thread, with 64-byte buffers; it does not select an observed
official locale. The UCRT/MSVCR71 equivalence for non-ASCII bytes remains
unproven. No UTF-8 decoding or Unicode casefold is introduced.

`444490`, evidence
`ev_e5dc17b4f0050c70a9390d200d7ec1ffd4b504913d3f65166bb0f8211a23227d`,
treats the result as signed:

- Positive result: clamp to the WORD C of the own resource, assignment of
  requested, pending computed against the restored current, then play=2.
- Zero or negative result: requested and pending stay intact, then play=2.

Thus a missing label stops Play without cancelling an earlier request. The
handler pops nothing and performs no bytecode jump. The scheduler eventually
consumes the request at its usual phase; GotoLabel does not immediately play
another frame or another script.

## Declared API and modern guards

`VmfDisplayRuntime` exposes `resolve_target`, `find_label` and
`apply_goto_label`. For an accepted origin, `{found=true}` owns a live
target; a native miss returns `{false,nullopt}`. Refusals for handle, kind,
closed context or quota are errors, never a false "absent". The const
`find_label` does not expose the temporary mutations of the native scanner.
`apply_goto_label` returns whether a label exists and keeps Stop even when it
is absent.

`VmfTargetLookupLimits` bounds the prefix before NUL, excluding that NUL; the
suffix after NUL is ignored, but a suffix after `:` before NUL counts toward
the quota. A finite view provides a logical terminator. Visits count every
active node examined and every parent step: `..` at root costs one, `/`
already at root costs zero. Zero quotas are allowed when no corresponding
operation is required.

`VmfScriptContext` is move-only, created from display, origin and
scriptIndex. It owns the immutable Program and keeps origin, current target,
phase and cursor private; `snapshot()` returns a copy of them. The Program
remains owned, but the runtime and the same shared stack must stay alive and
serialized. No stack pointer is used as an identity token, and no arena view
is kept between calls. The origin/context/Program is checked on every entry.
A stale target is not a native miss and does not become root.

The three entry points remain distinct: 19 scalar opcodes and 17 boundaries;
24 opcodes with an explicit target and 12 boundaries; 27 with a persistent
context and 9 boundaries. A script may target another timeline of the same
program: no artificial equality between the script owner and the target
resource.

The instruction and scan budgets are independent. An ordinary action ignored
in the skip phase falls under `skipped_actions`/`scan_limit`. At End, `8B` or
`20`, the context returns to execution: if the instruction budget is
exhausted, it stays on that unconsumed action with `instruction_limit`. The
failed SetTarget already counts among the executed instructions; only the
elements actually ignored feed `actions_skipped`. Fields added at the end of
the aggregates preserve the position of the pre-existing fields.

The pop of the `20` keeps its owning `VmfTextReference` until the end of the
resolution. A plain string_view is not enough: a linked_text source may have
the popped slot as its last owner. This temporary owner is not kept in the
context after the handler.

Visit quotas are **per lookup**, not cumulative over an invocation. The cost
can therefore grow with instructions × visits; label searches also traverse a
bounded but repeatable collection. These limits offer no frame budget and no
smoothness guarantee. The future driver will have to bound and measure the
overall work without erasing the continuation state.

End restores the origin target; a budget stop or `needs_host` keeps the
current target. An error keeps the effects already published and leaves the
context faulted; no restoration on error is promised. Call, services,
reentrancy and scheduling remain boundaries, with no fictitious success.

## Validation status

The independent oracle is frozen before the new CPP files in
`vmf-target-execution-tests/oracle-freeze-v1.json`, SHA-256
`54c0f25a0e2c08613d2ef7cc9d7786c0f111fa1042cf4da0d019f4a92e8f78c9`.
Its author did not read the production code. The frozen harness, SHA-256
`6fd94f0e8a0a39ba749969add3e77d71f8bc4f6b611e005b923c02464af9c851`,
remains identical after its first builds and runs.

Two reviews independently found a lifetime defect in the first TellTarget
implementation: the view outlived its owner after the pop of a linked text.
The owner is now kept until after the lookup. The fix precedes any run of the
harness; the harness also covers an external linked source already destroyed,
without changing the native expectations.

Each architecture, x64 and Win32, passes **80 fixture checks**, then
**3,178 checks in total**, the 80 included, with no failure and an empty
stderr. The cases cover whole names and their order, DF and its bounds, the
cache, 63-byte labels and the own resource, partial effects, independent
quotas, resumptions, End, invalid identities and the preservation of the
earlier boundaries.

The 62 x64 executable targets are built explicitly, without ALL_BUILD. CTest
passes **65/65 on x64** and **4/4 on Win32** on the modified components and
their direct dependencies. The logs and EXE identities remain in
`vmf-target-integration/`. No new invocation of the commercial game, full
navigation, smoothness in real conditions or co-op session is validated by
these tests. The provenance and cost limits above remain.
