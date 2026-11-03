# Menu-command helpers: memory corroboration

## Provenance and method

PC MC1.66A reference: SHA256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The four windows come from the [official observation](official_menu_actions_runtime.md).
The analysis is static on these captured bytes; it calls no game
function and does not reread the process.

The Ghidra provider rejects raw x86 files here. The analysis therefore constructs
analysis-only PE32 files, never executed, with one `.text` section at
the window VA and an artificial base. Headers do not come from the
game. The initial version contained padding; v2 has virtual and raw
sizes exactly equal to the supplied 32 or 128 bytes. The review checks all
four containers, byte-for-byte correspondence, Ghidra blocks and no
reading immediately before/after each window. Headers, zeros and
neighbors after RET never serve as behavioral evidence.

## Three complete bodies

| Entry | Inclusive body | Bytes | Body SHA256 |
| --- | --- | ---: | --- |
| `635A80` | `635A80..635A8E` | 15 | `9c00ce0ab12022ccf979a9c4491f53da84976a4eeccedce6009f338d98645b1e` |
| `634DA0` | `634DA0..634DB8` | 25 | `f603647484619d98b58a46e6c4dd4ef089992870daadda07cc496344a2423e36` |
| `634D80` | `634D80..634D98` | 25 | `7cbfe8ab350dc5eb8df636c2d78ebec73384930ee42661da3dc4586b4d8cf2e6` |

`635A80` reads DWORD `[ECX+194]` once and returns EAX1 exactly when it
equals 8, otherwise EAX0. A low-byte comparison would differ for
`0x108`. The body writes no field and receives no stack argument.
RET consumes its return address: ESP after RET is function-entry ESP
+4. Composed with the [callers](vmf_fscommand_menu_actions.md), ECX is
owner+4, so the compared field is owner+198.

`634DA0` and `634D80` take 32-bit W, push W then M+34, with ECX also
set to M+34. They call `63DC10` and `63DBC0` respectively, remove both
pushed words, then return 1 if external EAX32 equals 0, otherwise 0.
The calculation uses all 32 bits: external `0x100` produces 0. RET4
removes the return address and W argument, so callers perform no
additional cleanup. Both targets remain unknown.

## Retained boundaries

`622B20/128` remains an incomplete prefix: two flows leave the window.
Global pointer `1027FA0` is reread after first call `674BBE`; caching
the target would not reproduce this branch. The visible path then
calls this target or `6225B0` and retains a `674F67` call before its
only visible RET. None of these external bodies is established.
Visible direct reads do not describe their callees' implicit inputs.

Word F98 remains opaque on 32 bits. The caller clears it on the
initial owner after normal return, independently of the service result.
A required interface to missing services is not their implementation.
No constant interpretation, authentication or successful network
operation is inferred from these helpers.

## Validation

The review passes 520 integrity and bounds checks. Among 466
references, 465 current files match; the mutable journal was subsequently
extended and its exact historical prefix is rechecked against
the expected SHA, without claiming its current file is unchanged.
Initial opening errors and temporary scope deviations remain archived.
The eight review scenarios are unexecuted expectations. No additional
service C++, playable-menu trial or co-op test is claimed here.
