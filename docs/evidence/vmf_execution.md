# Scalar execution of VMF scripts

## Result and scope

`execute_vmf_actions` executes actions from an already validated
`VmfMovie`, with a prepared stack and resumable cursor. **19 of the 36
observed opcodes** are implemented: End, text Push, Pop, calculations
0A..12, StringEquals/Length/Extract, ToInteger, StringAdd, Jump and If.
This count is opcode coverage, not interface or game completion percentage.

The 17 host actions return `needs_host` **before** consuming arguments
or advancing the cursor: frames/playback, variables, targets, properties,
Trace, Call and external commands. Trace is neither always pop nor always
no-op: its native logger condition still needs the host. No fake callback,
rendering, network transport or multiplayer success is added.

A second entry now receives a `VmfDisplayRuntime` and an explicit
timeline target. It takes the movie from that runtime's program and also
executes NextFrame, PreviousFrame, Play, Stop and GotoFrame: **24 supported
opcodes, 12 retained host boundaries** for this entry. The earlier scalar
API retains its 19/17 contract. Timeline commands
change the request and playback state; they do not play the requested
frame. Stop or a pending request does not interrupt bytecode. No stack
reset is added.

The target is validated before any instruction: it must be a live
timeline in the context, possibly cached. A missing, stale, foreign-context,
button-type or post-closure target gives a `timeline` error, its display
diagnostic and a faulted cursor, without other effects. The script may
belong to another resource than its target. Frame bounds always come
from the target's own resource. Program, cursor and context must stay
associated across resumptions. This entry does not yet resolve SetTarget
or schedule any queue.

A third entry uses a persistent `VmfScriptContext`. It adds SetTarget,
TellTarget and GotoLabel: **27 supported opcodes and 9 host boundaries**.
The preceding two entries retain their contracts. The context owns the
program and origin; the stack remains shared and caller-supplied at
every resumption. Targets and the
script context describe evidence and limits.

A fourth entry also receives a `const VmfVariables&` dictionary and
adds GetVariable: **28 opcodes, 8 host boundaries**.
The preceding three entries retain their contracts. This read uses the
current target without changing it on a native failure, which pushes
empty text. SetVariable remains suspended pending scope and cleanup integration.

A fifth explicit entry, `execute_vmf_actions_with_variables`, uses the
display-owned dictionary and adds
SetVariable and scope cleanup: **29 opcodes,
7 host boundaries**. The frame driver may select this backend through
configuration. The four other entries retain their contracts.

Every selection restarts from the origin, even after successful selection.
A native failure nulls the target and skips actions until the next
End/SetTarget/TellTarget, left unconsumed. Ignored actions have no effects,
including branches and host actions. `skipped_actions` bounds this
traversal separately; `scan_limit` allows resumption without replaying
TellTarget or its pop. The failed selection action counts once among
executed instructions, never among skipped actions.

Skipping advances even when the instruction quota is exhausted. At its
boundary, the phase returns to execution, then that quota is checked.
TellTarget resets the target to origin **before** popping and then
charges text bytes: a stack or budget error retains these completed
effects. A modern lookup error retains origin and faults the context,
without publishing a partial result. End restores origin; budget stops
or `needs_host` retain the current target. No scheduler, callback or
stack reset is added.

The visit quota applies to each resolution, rather than accumulating
across all instructions of a call. A sequence of selections may therefore
multiply the cost of both quotas. These limits guarantee neither a
frame budget nor absence of stutter; the future driver must bound and
measure this total work.

## Fidelity and references

Source: assemblies in Rea batch `rea/vmf-action-interpreter/session/`:
exports 11..13,24..34 for operations and Jump/If contracts from the
preceding batch. Secondary SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
Official trial 25 corroborates arithmetic,
Boolean, ToInteger, Jump/If bodies and four stack helpers, without
attested invocation. Text wrappers 445080/445130/4451B0/445260 and
formatter 442330 remain secondary. The
[detailed contracts](vmf_action_semantics.md) give VAs.

Calculations remain SSE float32 without FMA. Divide retains its unusual
negative-denominator clamp; Booleans are owned “1” or “0” texts. NaN
is treated as nonzero for logic and If; equality and Less remain ordered.
Both StringAdd/StringEquals references are resolved **after** the pops:
numbers 1 then 2 thus yield `11` and `1`, through native scratch aliasing.
Branch targets use validated compact space, not physical offsets.

StringLength counts bytes. Native code converts with x87; the maximum
2^24-byte budget guarantees an exactly representable integer result
regardless of rounding. StringExtract retains positive base-1 indices
and nonpositive indices at 0; negative length and out-of-string starts
are rejected before unbounded reading. Output remains limited to 31 bytes.

Xbox data is read in its own endianness and uses reconstructed PC
semantics here. This does not prove equivalence of the still-unimported
XEX code or complete Xbox360 controller coverage.

## Budgets and resumption

Every call bounds instructions and logical operand bytes. An instruction
limit retains completed effects and identifies the next action without
replay. End makes subsequent calls inactive. A cursor marked finished
must point to End; this guard was added after a failing test. The cursor
stays associated with the same movie; it carries no cryptographic identity.

An error retains prefix effects and marks the cursor faulted, without
automatically resuming a partly popped operation. No per-frame stack
reset is assumed. Execution does not allocate after data preparation.

## Validation and corpus

216 rational IEEE754 vectors, **864 expectations across four modes**,
are frozen before the CPP. JSON SHA-256:
`cbd2cf8562d0ea102a3ad5334171702287dd481beb3fe79f36dc7585bb8eac09`.
The initial harness is preserved; additional guards add 42 checks.
The “finished before Push” case fails twice on x64 before correction;
no numeric oracle is changed. **10240 checks pass per architecture**,
x64 and Win32: two data profiles, aliasing, compact branches, budgets,
resumption, errors and stopping before each of the 17 host opcodes.
NaN payload and the order of unmasked traps are outside the contract.

Diagnostics on the 39 unchanged private files: **27685 isolated entries
with an empty stack**, same results on both architectures. 147 reach
End, 27538 await a host, with no 1000-instruction overflow or fault.
11022 instructions are actually executed; the rest is not simulated.
Reaching End may include an empty script; this validates no complete
menu. These entries do not represent the game's actual scheduling or state.

Dominant first boundaries are Stop 10787, GetVariable 4228, Play 3317
and SetTarget 2633. They motivate the next sprite-context and variable
port. CTest: `vmf_scalar_execution`; integrated x64 suite **58/58**.
