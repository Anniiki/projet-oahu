# VMF scripts: actual usage and interpreter boundaries

## Scope and result

Static inventory of 30 October 2026 over the **39 attested VMF copies**: 20
Xbox 2.5 and 19 PC 2.8, **23,844,792 bytes**, described in
[vmf_format_inventory.md](vmf_format_inventory.md). No action, host command,
window, console, game or GPU is executed. No new Rea session is opened; the
analysis of the native handlers is kept separately, outside this note.

The corpus contains **27,685 streams**: 18,822 timelines and 8,863 button
conditions, **520,158 instructions** and 31,816 branches. The regions are
re-decoded from the copies, with their own bounds; the older inventories based
on a string scan are not used.

A faithful execution requires a per-instance variable environment, live
targets and timelines, properties, host callbacks and native conversions. A
36-opcode dispatcher or an Xbox glyph display is not enough to reproduce the
menus.

## Operands and frequency of the 36 opcodes

The names are SWF reference names, **not a claim that their native effects are
equivalent**. The counts are encoded sites, not frequencies measured at run
time; End is included.

| Opcode | Reference name | Occurrences |
| --- | --- | ---: |
| `00` | End | 27,685 |
| `04` | NextFrame | 429 |
| `05` | PreviousFrame | 422 |
| `06` | Play | 21,110 |
| `07` | Stop | 11,366 |
| `0A` | Add | 3,602 |
| `0B` | Subtract | 2,006 |
| `0C` | Multiply | 169 |
| `0D` | Divide | 168 |
| `0E` | Equals | 11,567 |
| `0F` | Less | 4,086 |
| `10` | And | 1,138 |
| `11` | Or | 22 |
| `12` | Not | 19,012 |
| `13` | StringEquals | 3,000 |
| `14` | StringLength | 24 |
| `15` | StringExtract | 12 |
| `17` | Pop | 761 |
| `18` | ToInteger | 2 |
| `1C` | GetVariable | 57,251 |
| `1D` | SetVariable | 38,233 |
| `20` | SetTarget2 | 7,704 |
| `21` | StringAdd | 17,715 |
| `22` | GetProperty | 402 |
| `23` | SetProperty | 4,077 |
| `26` | Trace | 3,523 |
| `81` | GotoFrame | 649 |
| `83` | GetURL | 10,429 |
| `8B` | SetTarget | 42,775 |
| `8C` | GotoLabel | 27,532 |
| `96` | Push | 168,007 |
| `99` | Jump | 13,395 |
| `9A` | GetURL2 | 1,538 |
| `9D` | If | 18,421 |
| `9E` | Call | 10 |
| `9F` | GotoFrame2 | 1,916 |

Every encoded Push is a type-byte-0 string: **38,234** have a complete ASCII
numeric syntax, 37,960 of them integers and 274 other forms; 3 have non-ASCII
bytes. This lexical classification does not simulate the native conversion.
The raw 9A operands are all 0; 9F carries 0 on 1,130 sites and 1 on 786.
GotoFrame (81) contains the raw values 0, 1, 2, 3, 11, 12; no 0-based/1-based
convention is added to these observations.

## Necessary relations, beyond strings

The abstract graph leaves host reads symbolic: it does not propagate stores
like a real environment and evaluates neither numbers nor conditions. Under
this conditional model, it links the following uses:

| Boundary | Corpus facts and execution need |
| --- | --- |
| Variables | 7,631 scripts read, 7,241 write. 56,208 reads have at least one recovered literal name; 1,043 remain computed/unknown. The recovered literal names are all ASCII. |
| Targets | 10,249 scripts change target; 790 distinct SetTarget operands, including 25,210 empty resets and absolute/relative paths. The model's last target request is **not** a resolved instance. |
| Properties | SetProperty: raw indices 7→3,576 sites, 2→170, 6→111, 0→90, 1→86, 3→42, 10→2. GetProperty: 13→254, 11→59, 4→33, 0→32, 1→14, 2→8, 3→2. The native meaning must come from the handlers, not from the modern renderer API. |
| Host commands | 6,351 scripts contain 83 or 9A. The 10,429 opcode-83 URLs are literal FSCommands with an empty target; the 1,538 opcode-9A URLs have an FSCommand prefix and an empty target in the abstract graph. The suffixes may depend on variables. |
| Timelines | The 1,916 GotoFrame2 all depend on loaded/computed values in this model; none resolves to a literal. The 10 Call have 8 literal labels (`delete/up/down/back`, two each) and 2 `/:MsgResponse` values. |

Presence is not resolution: 264 GotoLabel, 71 distinct names in 19 movies,
find no label of the same name anywhere in their own movie. This is not proof
of a bug or of dead code: the target may belong to another instance/resource,
be created later, or the path may not be taken. The native policy for
absent targets/labels has to be determined.

The normalized bodies sometimes coincide across platforms: map 570 common
streams, 123 additional on PC and 27 on Xbox; avatar 535/535 common bodies.
This does not make the interfaces equivalent: owners, conditions, frames,
host context and resources may differ.

## Concrete witnesses for future execution tests

The offsets below are **VMF file offsets**, not VAs. The exact owners,
conditions and instructions remain private in
`vmf-script-usage/case-witnesses-v1.json`.

- **Map/zoom**: Xbox map, button 997, tag `C5FD4`, conditions 1/2; PC
  button 1060, tag `BD5FC`. Read of WhatZoom, comparison, store to `zoom_tmp`,
  command `zoin/zoou`, then a **new read** of WhatZoom for the `curseur`
  target and the sound decision. The encoded upper bound is 5 on Xbox and 4
  on PC. A host response or a variable cache deferred to the next frame would
  change this order. The flags remain raw, with no assumed physical binding.
- **List/menu and built command**: PC car_show, button 95, tag `9D48`.
  `cur_line` contributes to the line/bar clip names; the stores are followed
  by SetTarget2, GotoLabel and Play. At `A41C`, GetURL2 receives a URL built
  from `FSCommand:`, `widg:`, `msg` and an action suffix. Replacing this path
  with a fixed list of commands is not enough.
- **Lobby**: PC CHALLENG, sprite 1214/frame 0, tag `B7B48`. The script
  compares roles, modifies several properties/targets, calls `inpl`, then
  loops over button names built from the player index. It reads both
  `/:whatplayer` and `/:WhatPlayer`; the donor carries the same body in
  sprite 1079, tag `C8C50`. This does not prove any active online session.
- **Computed variable name**: PC Club, button 555, tag `4A82C`, read at
  `4A8B4` of `line + cur_line + /:IsValid`. A dictionary of literal names
  alone does not cover these 1,043 computed reads. Forms such as
  `..:total_lines`, `../../../:member_action` and `/:...` must keep their
  scope grammar.
- **Input/backspace**: PC HUD button 783, tag `6EB34`, first stream `6EB80`:
  reads TxtInput, pushes index 0, computes StringLength − 1, calls
  StringExtract at `6EBC8`, stores at `6EBCA`, then requests the backspace
  sound. The 12 StringExtract are therefore not a negligible option for a
  usable UI. Length units, indexing and clamps remain native facts to
  establish.
- **Computation/UI**: PC Home, sprite 1478/frame 1, tag `FFEE4`: computation
  from `achvDone` and `achvTotal`, add 0.5 then ToInteger at `FFFBC`, before
  a gauge property. The other ToInteger is its Xbox counterpart. The car_show
  scrollbar divisions (`63FC`) also read host denominators; generic C++
  arithmetic does not prove the fidelity of the results.
- **Dynamic Call**: Xbox Club, button 1376, tag `132E00`, condition 2: host
  command, target change to `../..`, read of MsgResponse at `132F30`, Call at
  `132F32`, then new target/label requests. The order of effects and the
  restoration of call contexts have to be closed.

## Native deviations: do not substitute a standard SWF engine

Earlier native analysis provided anchors in
`rea/vmf-action-interpreter/session/`, for the **secondary binary f452** only at
this stage: SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
They are not promoted transitively by captures of other bodies.

- Push string via `44B2E0`, exports 14/20, copies at most **31 bytes**, not
  31 Unicode characters. Native types observed: `1` float32, `4` owned
  string, `2` linked string; GetVariable may copy or link depending on the
  type. The lifetimes of host values are not represented by the owning reader
  alone.
- Equals/Not produce the strings `"1"/"0"`, not simply a boolean of the C++
  library. The conversion `442530` is specific, notably for the letters
  `e/E`, signs, dots and float32 rounding; no equivalence with `strtof`,
  `stod` or modern ECMAScript truthiness is claimed.
- Trace `445920`, export 56, only pops if the flag `FABFB4` and the required
  logger are present. The SWF stack model must therefore remain conditional.
- The variable lookups `4435D0/4434B0`, exports 89/90, compare by `_stricmp`
  **and instance ID**. 34 groups of ASCII names in the catalog differ by
  case. This local comparison is established; the hash and locale of the
  complete lookup remain to be closed before asserting their aliasing
  everywhere. The child search `447FE0`, export 91, uses a masked bytewise
  comparison and precise separators, not a generic Unicode casefold.
- Earlier native analysis also established a division with a particular clamp
  of the denominator, and specific number/label routes for Call/GotoFrame2.
  Raw operands alone must not decide these effects; they remain within the
  scope of the detailed native contracts established separately.

### Precise impact of the 31-byte copy

Over 168,007 Push, **only four exceed 31 bytes**, all of length 32: one and
the same string, two PC Club conditions (`DAF64/DAF94`) and two Xbox Club
conditions (`10FA9C/10FACC`). Each stream is exactly **Push → Trace → End**.
Its raw SHA-256 is
`a511d7e20534221801b516566a1770f6d76ac7a213d958c5652156252da259f0`.
The text is not reproduced here. No variable name, target or functional
command of these streams depends on the last byte; if Trace is a no-op, the
value nevertheless stays on the stack, pending a native policy that
remains to be closed.

Conversely, **50 direct SetTarget** exceed 31 bytes (maximum 39); they do not
automatically go through Push. GotoLabel maximum 21, opcode-83 URL maximum 25.
The 31-byte rule must not be applied indiscriminately to every source operand.
The length/copy of the StringAdd result remains a distinct dependency.

The raw Push contain `e` 70,834 times and `E` 4,982 times, often in names.
Requiring at least one digit in the initial numeric prefix: 38,326 sites, 92
with a suffix, 10 with `e`, none with `E`. The ten are names of the form
`2modes…`. In the flow model, **no literal candidate for numeric consumption
contains e/E**. This neither describes the host values loaded at run time,
which remain unknown, nor validates another converter.

## Stack graphs: conditional result and counter-example

The models choose no branch according to its value, do not execute the called
frames and call no host. They assume an empty stack on entry and the SWF4
arities, with the Trace case explicitly selected.

| Model | Bounded result |
| --- | --- |
| Trace pops | 27,685 empty ends, no underflow and no join of contradictory heights; maximum 6. 520,154 sites reachable in the CFG, 1,487 back edges in 1,029 scripts. |
| Trace does nothing, other arities unchanged | 3,174 scripts have a possible non-empty end; 647 have joins of multiple heights; 83 reach the analysis limit 64. This is **not** a demonstrated native leak. |

The first model confirms a consistency of the chosen grammar and arities, not
a native contract. The second shows why the first must not be taken to imply
a runtime with a stack capped at 6 or a Trace that always pops. Called frames,
reentrant callbacks, stack ownership/clear and the logger configuration
remain necessary. The CFG leaves four sites unreachable by its edges alone;
no code is removed or actually declared dead.

The value analysis performs 569,603 transfers, without reaching its ceiling.
It keeps bounded alternatives/expressions and symbolic host reads; no numeric
result is evaluated, the stores do not become a real global state, the last
SetTarget is not resolved and a Call invalidates the following context
assumptions. The 9A FSCommand prefixes and computed names are therefore
conditional syntactic relations, not callbacks that were actually run.

## Priorities for the next C++ reconstruction

1. **Native values and stack**: 31-byte copy, linked/owned strings,
   float→string formatting, numeric conversion and string booleans; results
   of StringAdd/Extract/Length/ToInteger, errors and stack/Trace clear.
2. **Per-instance environment**: exact keys preserved, native
   comparisons/hash, `:`/`/`/`..` scoping, variable creation/replacement and
   immediate read after callback. Do not treat all variables as global.
3. **Targets/properties/timelines**: stable identities, explicit resolution,
   raw properties then typed by evidence, absent labels, frame base,
   Play/Stop/Next/Previous, and Call context/restoration.
4. **Host boundary**: direct and built FSCommand, order of effects,
   reentrancy, value lifetimes and refusal of a missing service. No arbitrary
   success or physical gamepad mapping is added.
5. **Bounded execution**: the largest streams have 2,079 instructions and
   loops; instruction limit, stack/calls and interruption policy must be
   explicit as port guards, without attributing them to the native code.

## Evidence, quotas and immutability

New private batch `vmf-script-usage/`:

- `inventory-v1.json`: 36 opcodes, operands, profiles, labels, normalized
  bodies.
- `programs-v1.json`: source-bound programs, private only; no executor.
  SHA-256 `caa5b2a060ff25bcea235564415c3bc25c7698be64d571ae81d43c79e2d7c754`.
- `height-model-v1.json`, `trace-noop-height-v1.json`: distinct models, the
  initial hypothesis was not rewritten as a native fact.
- `abstract-flow-v1.json`, `case-witnesses-v1.json`,
  `push-copy31-audit-v1.json`: relations, witnesses and 31-byte impact.
- `model-selftests-v1.json`: **10 source-owned checks passed** on the static
  tools alone, including absence of store propagation, two branches preserved
  and the Trace pop/no-op distinction. These are not VM tests.

Analysis bounds: 64 MiB/file, 128 MiB/validated report, 50,000 streams,
1,000,000 instructions; outputs capped per batch. The height graph is limited
to 64 stack slots and 8 million states; the value graph to 4 alternatives,
160 bytes of representation, 512 of composed literal, 16 dependencies,
8 origins, 32 updates/site and 4 million transfers. These budgets commit
neither the native code nor the peak RSS of the JSON/Python objects.

The 39 SHA are verified before/after; the resources, manifests and older
ledgers remain unchanged. The programs, detailed strings and large reports
remain private, outside the repository. Only this document is intended for
contribution; no C++, CMake, index, commit or other document is modified.
