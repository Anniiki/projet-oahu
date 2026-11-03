# VMF timeline commands: target, request and IP

## Scope

The five commands 04/05/06/07/81 prepare the hookup of the bytecode to
the display runtime. They modify a request or Play/Stop; they do not play a
frame, do not rebuild the list and consume no action queue. The native code
studied is the **secondary PC** binary, SHA256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
No official invocation or equivalence with the XEX code is inferred here.

Main sources: `rea/vmf-host-context/session/`, assemblies 05/06/11/12/13.
The matching pseudocode of the `vmf-action-interpreter` batch serves as a map,
but the signed branches and widths below come from the assemblies.

| Action | Body | Evidence ID |
|---|---|---|
|04 NextFrame|4442C0|`ev_f9466fdc35511c2731f855ea2add3b0bdea4e3fc948650e1b73d9d6104c44b26`|
|05 PreviousFrame|444310|`ev_ca70226658c28611f2758b01b64ffb84fb62ef95e3d434841fe558daea5a8df1`|
|06 Play|444360|`ev_1aaf0944cb846a0bb298d348a725792c45258cdf3d27cebcd0e03aed5ff764e4`|
|07 Stop|4443A0|`ev_aa13468e1fe63bde3e90ea37237c8185dfce5a285f3bdc53529dc5d6b265d5ac`|
|81 GotoFrame|444110|`ev_e3893578187c3391a52a5d3898b8f2d9c107e2758e27a47aab951f9c8e76ea19`|

## Target and state spaces

Each primitive takes its target from `G=[10E8078], T=[G+58]`. This is neither
a stack top nor the characterID of a node. The frame count is the WORD of
**T's own resource**: `R=[T+18], C=WORD[R+0C]`, even if the node that keeps
this child has since changed resource.

The fields are current1=`T+60`, play=`T+64`, pending=`T+68` BYTE,
requested1=`T+70`. The current frame and the request are distinct. The name
"Next" does not mean incrementing requested. The frame count is denoted C.

The handlers do not test T against NULL and do not check its kind before
dereferencing. A null target is therefore not a proven native no-op and must
not implicitly become the root. The provenance of the selection is a separate
contract: 443AF0 captures the entry target in Q+14; the SetTarget 4443E0 and
TellTarget 444430 pseudocodes start again from that origin, then 443FE0
resolves the path. Their failure goes through 444090, which jumps to
End/SetTarget/TellTarget. The complete assemblies of this path remain to
be corroborated; this mechanism must not be replaced by "execute on root
anyway".

The variants with diagnostics call 4374F0 and then re-read the context. The
initial API disables this diagnostic: it adds no reentrant logging callback
and does not claim to reproduce its possible context mutations.

## Exact branches of the five primitives

| Command | Writes |
|---|---|
|Play|play=1 only; current/requested/pending preserved|
|Stop|play=2 only; current/requested/pending preserved|
|Next|candidate=INC32(current). If the signed candidate is <=0: no write. Otherwise requested=signed min(candidate,C), pending=(requested!=current). play unchanged|
|Previous|candidate=DEC32(current), same branch and ceiling as Next. play unchanged|
|GotoFrame|unsigned WORD operand +1 in 32 bits; if positive, ceiling C then requested/pending as above. Then writes play=2, even without a change of request|

Goto reads a WORD, so FFFF+1 is 65536 and not 0. Its candidate<=0 branch
exists in the body but is unreachable with this operand. No minimum of 1 is
added after the ceiling; validated movies have C>=1. A file with C=0 is
rejected upstream by the reader, without inventing a native clamp.

Discriminating cases within the runtime's domain:

- Previous at current=1 gives candidate=0: **requested and pending stay
  exactly as they are**, including an already pending request. This is not
  an assignment requested=1/pending=false.
- Next at current=C requests C and clears pending. Previous at current=C+1 and
  Next at current=C+1 both request C, pending=true.
- C=1/current=1: Next sets requested=1/pending=false again; Previous keeps
  the previous state. Goto 0 sets requested=1/pending=false again, then Stop.
- Goto to the current frame clears pending but always stops Play.
  A Play executed afterwards reactivates Play without cancelling that request.
- Two successive Next without a timeline step request the same frame twice,
  because current has not changed. Neither pending nor Stop ends the script.

The native INC/DEC are 32-bit, followed by signed TEST/JLE. Overflows of
invalid machine states must not become signed C++ overflow; the initial
runtime only admits current1 in 1..C+1 and requested1 in 1..C.

## IP, stack and executor admission

None of the five bodies reads or writes the stack, the action IP or its
advance flag. The common advance 443A90, assembly 40 of the
`rea/ui-map-input-dispatch/session/` batch, is exact:

- short opcode 04..07: +2 physical bytes, +1 compact unit;
- opcode 81 with a 2-byte WORD payload: +6 physical, +5 compact;
- in both cases: a single action consumed.

Proof for 443A90:
`ev_1b3d602a8e99f46d46147131976e3dc641c270bd6710a2f339ad39a30f491cc4`.
Its body and the two spans of the dispatcher 443AF0 have their presence
corroborated by [trial 25](official_trial_25.md), without attested invocation.

Assembly 02 of the `rea/vmf-script-calls/session/` batch closes the common
steps of 443AF0, evidence
`ev_f39bbd061fbd28e2246c82f787dccff246df3eab1afe02b167cde693ae673c19`:

- 443AFF..443B07 captures T in Q+14; Q+10 receives 1 at 443B18.
- The CALLs 443BB8/443BC2/443BCC/443BD6 to the four short primitives all
  rejoin 443DA2. Goto receives IP+4 at 443D2A..443D2D and also rejoins
  this block. The opcode associations of the existing map are consistent
  with these calls; this export does not reproduce the jump tables.
- 443DA2..443DAE calls 443A90 only if Q+10==1, then sets Q+10 back to 1.
  The loop then tests End at the IP; it tests neither play nor pending.
- 443DBF..443DC7 restores [G+58] from Q+14 at the normal loop exit.
  If the script already starts with End, 443B1F jumps to the return without
  a handler.

This normal restoration guarantees neither restoration on exception nor
reentrance of the Q context. Diagnostic callbacks may also modify the
context; their behavior is not provided by the initial API.

The C++ reader validates the 2-byte payload of 81; the hookup must read
it in the endianness of the Movie profile, then perform the addition in 32
bits. The next_action indices of the C++ designate validated actions: advance
by 1, do not add the physical/compact offsets to this index.

## Contract of the initial API

`VmfDisplayRuntime::apply_timeline_action(target, action, frame_index)` exposes
these primitives without advancing a timeline. The target must be a live
timeline handle of this context; a cached timeline is admitted without being
reactivated. The initial mode refuses an invalid enum, a null/stale/foreign
handle, the button kind and a closed context before any mutation. These guards
are modern, distinct from the unguarded dereferences of the native code.

The new overload `execute_vmf_actions(display,target,cursor,stack,limits)`
takes the Movie of the immutable Program owned by display. The program()
getter returns an owning shared_ptr, including after close. The target is
validated before any instruction; a refusal marks the faulty cursor, keeps
the stack and the display, and exposes the nested runtime error. No root
fallback exists.

A sprite's script may target another timeline of the same program: do not
require script.parent_sprite==target.resource. This explicit target
represents the target already chosen by the host; the hookup does not
yet provide SetTarget, its grammar or its child lookup.

Each command consumes an instruction only when it is admitted. An
instruction limit stops before its effects and keeps the next action; a
resumption does not replay an already consumed command. Errors keep the
prefix and forbid retrying the faulty cursor. The caller keeps the same
program/stack/target for a budget resumption. The old overload stays at 19
opcodes and 17 host boundaries; the new one adds these five commands and
keeps the 12 other boundaries before effect/advance. No stack clear, rewind,
queue consumption or implicit tick is added.

## Validation of the C++ hookup

The `vmf_timeline_execution` test passes on x64 and Win32: **4,422 checks
per architecture**, including 90 for prior validation of the fixtures. Both
builds, the fixtures alone and the full runs succeeded on the first pass. The
18 numeric cases are manual constants taken from the assemblies, frozen
before the production delta; the harness was written without reading those
CPP files, then frozen before compilation.

The cases cover root, child and cached instance, earlier requests, the End
boundary, a single frame, FFFF without wrap and two Next without a tick. The
endianness test uses 300 frames and the operands 0100/0001: a ceiling that is
too low cannot mask an inversion. Full snapshots verify the preserved fields.
Budgets, backward branch, resumption, target errors and the preservation of
the 17/12 boundaries of both entry points are checked separately. Target
refusals before any instruction report a zero opcode and a zero counter.

Independent static review: no confirmed P1/P2 defect. The 61 executable
targets of the project were rebuilt explicitly. CTest x64: **64/64**;
targeted CTest Win32: **3/3** (scalar, display, timeline). These component
checks do not demonstrate a displayed or playable menu.

## Open limits

The player 448B20/448E60 consumes the requests at precise phases; see
[the scheduler](vmf_scheduler.md). The hookup of the commands does not
allow immediate processing of the request, stopping the bytecode at Stop or
ticking all cached instances. The reentrance of 443AF0/Call, the restoration
of its IP/stack, the SetTarget paths and the diagnostics policy must be
integrated with their exact contracts. The established facts about entry/exit
and the collection of Call are detailed in [script calls](vmf_script_calls.md).
