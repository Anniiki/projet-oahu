# VMF: interpretation, calls and script queue

## Scope and provenance

This contract complements [the scheduler](vmf_scheduler.md). It describes the
static exports of the Paradise secondary PE, SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
The primary reference remains the official PC MC 1.66 A executable, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
This batch adds no evidence of a call, of in-game script reading or of Xbox
compatibility. The in-memory code equalities documented earlier do not extend
to the called functions.

Private evidence is under `reprise-2026-10-31/rea/vmf-script-calls/`: fifteen
successful queries, export and session closure, then a Rea / secondary PE /
DUMPBIN comparison of **1,384 bytes**, five ranges and four bodies:

| Routine | Inclusive ranges and sizes |
| --- | --- |
| Interpreter `443AF0` | `443AF0..443DE7`, 760 bytes |
| Call `445640` | `445640..4456F9`, 186; `445700..445777`, 120 |
| Stack initialization `44B1A0` | `44B1A0..44B1FB`, 92 |
| Root constructor `441700` | `441700..4417E1`, 226 |

The Call body holds 306 bytes; the six bytes `4456FA..4456FF` are not part of
its AddressSet. The report `exact-byte-validation.json` keeps the export and
byte identities; the final snapshot has SHA-256
`e97781f708cc7fd4e3250978025bc561ec5c2401c9a0ae3e4d7b6235be87f394`.
Our independent cross-check of the exports and dumps alone is in
`vmf-script-calls-review/validation.json`, with no new read of the PE.

## Contexts and shared stack

We write `G` for the movie context, `T` for a timeline and `Q = T+34h`. The
following fields are native x86 offsets; no native pointer is a portable
handle.

| Field of Q | Established use |
| --- | --- |
| `+00`, `+04` | Script base, physical instruction pointer |
| `+08`, `+0C` | Action ordinal, compact position |
| `+10`, `+14` | Automatic advance, saved current timeline |
| `+18`, `+1C` | Array of script pointers, script count |
| `+20`, `+24` | Pointers to the two shared objects S and D |

`441700` passes `G+98h` and `G+A8h` to the common constructor `448680` for the
root `T = G+B4h`; the loader `4418B0` does the same for the pool timelines.
`448680` stores them at `T+54h/+58h`. The stack S is therefore shared by these
timelines, with no demonstrated per-invocation stack.

`44B1A0` receives S in EBX and the arena in ECX: it resets its counter to
zero, reserves the raw size configured at `G+04h`, then initializes the text
start, end and cursor. It is called during movie initialization; neither
`443AF0` nor Call calls it. A failed reservation can yield a null base, with
no local guarantee of a usable empty stack.

## Interpreter entry and exit

`443AF0` receives Q in ECX and the script in EAX. It replaces the base, IP,
ordinal and compact position of Q, enables automatic advance and copies
`G.current` (`G+58h`) into `Q+14h`. A first null opcode returns immediately
after these writes. Otherwise, each handler is followed by the advance
`443A90` only if `Q+10h == 1`, then that flag is set back to one.

On normal non-empty termination, it reloads the global context and restores
`G.current` from **the Q+14h field present at that moment**. It restores
neither the other fields of Q nor the stack S. The absence of nested saving
makes a reentry on the same Q destructive for the outer context; this body
provides no guard. A possible protection by the host is not established.

## Call: temporary selection and deferred collection

Opcode `9Eh` calls `445640` with ECX = Q. It pops a text through `44B250`.
When a `:` separator exists, it hands the target to `443FE0` and uses the
suffix as the frame designation. A failure of that resolution restores the
current timeline but does not push the popped text back onto S.

The numeric route accepts only ASCII digit and dot bytes, including the empty
string. It calls the native converter `442530`, applies `CVTTSS2SI`, then a
32-bit increment. A sign, a space or an exponent takes the label route, whose
helper is **`448990`**. No explicit bound on FrameCount appears at that site.
The result of the label lookup and its absence cases remain to be closed.

Call saves the target's cursor `T+5Ch` and frame `T+60h`, replaces them
temporarily, then walks the records from its frame table: a virtual getter
equal to zero terminates; type 12 invokes the script producer; type 39 sets
the cursor to `record + DWORD[record+10h]`; the others go through the link
`record+04h`. It does not invoke the placement, removal or ShowFrame handlers,
and contains no special termination on type 1. This expression over relocated
records must not be replaced by an assumption about the raw file extent or a
standard SWF rule.

On the normal path, Call restores the target's cursor and frame, then the
original current timeline. It does not restore Play, pending, requested, the
fields of Q, the stack or the queue element count. **It does not call the
interpreter recursively: it can add scripts to be executed later.** The
effects of a non-local abandonment are not covered here.

## Capacity and mutation of the Q queue

The producer `443A20` appends `record+08h` to `Q.array[Q.count]` only if
`T.pending == 0`, then increments the 32-bit counter. Its 56-byte body checks
no capacity and does not reallocate. It always advances the cursor.

The loader reserves `4*C` bytes for the root; pool objects have a stride of
`E8h+4*C`, and `448790` places their array at `T+E8h`. **C is the configured
DWORD `G+14h`**, not a field of the VMF header. Its default value, its other
writers and a global no-overflow guarantee remain unknown.

`443A60` captures once the start and the end computed from the initial count.
It loads each script pointer at the time of its visit, calls `443AF0`, then
resets the counter to zero after the loop. This is neither a complete snapshot
of the scripts nor a drain until stabilization. With a stable array and
sufficient capacity, additions beyond the initial end are not visited and are
forgotten by that reset. A reentrant drain of the same Q can revisit the old
elements; no local guard is visible.

## Boundaries of the future port

A safe port must bound the arrays, make identities and lifetimes explicit and
define error handling without claiming that these guards existed in the native
code. It must also preserve stack sharing and partial effects, or announce any
difference. Full resolution of targets and labels, the possible host mutations
during callbacks and the behavior of all handlers remain outside this batch.
The timeline commands already reconstructed therefore do not yet constitute a
complete VMF player; no simulated Call or permissive success follows from this
study.

The [frame player](vmf_frame_player.md) now integrates script execution and
the captured-bound drain, with a shared stack and bounded resumption. The
[targets and labels](vmf_script_context.md) are connected; Call and the host
services remain open. The chronology and the native reentrance limits
described here continue to apply.
