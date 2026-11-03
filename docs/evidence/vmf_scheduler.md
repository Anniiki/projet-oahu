# Native VMF scheduling: frames, actions and children

## Status and evidence

This map is now connected to the [C++ frame driver](vmf_frame_player.md),
validated on components and confronted with the first boundaries of the corpus.
**No complete menu or in-game trial is validated by this document**. It comes
from the secondary PC binary, SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
It does not constitute an official invocation or an equivalence with the Xbox
code. The Xbox data remains a distinct resource profile.

Private evidence under `<private-evidence>/reprise-2026-10-31/rea/`:

- `vmf-display-lifecycle/session/47`, `61`, `73`: 448E60, 4476C0, 448B20;
  their bytes are covered by the
  [display lifecycle](vmf_display_lifecycle.md) validations.
- `vmf-host-context/session-v2/27`, `32`: initializer 4487F0 and 4418B0.
- `vmf-runtime-scheduler/session/05..32`: predicate 446A60, driver 441DF0,
  selection tables, vtables, End/Action wrappers and constants. The session
  closed normally after 32 requests. Its
  `exact-byte-validation.json` covers 389 bytes of three bodies, 224 bytes
  of code windows and 217 bytes of data. The wrapper windows contain
  134 bytes of delimited bodies, excluding padding; the other 48 bytes
  overlap the period computation in 4418B0.
- Action consumer reused, without new analysis:
  `reprise-2026-10-30/rea/vmf-action-interpreter/session/98-procedure_assembly.json`,
  443A60/42 bytes, evidence
  `ev_5d55565103dfe13497dbc4abf01acb46433c7b11f4d6212088a3c38fae9f2dee`.

The private review `vmf-runtime-scheduler-review/` recomputes the
tag→branch→vtable correspondences and independently compares the JSON bytes
against the dumps already exported, without opening a PE or executing
commercial code.

## Context spaces and indices

Do not confuse the same offsets in the following objects:

| Object | Observed fields used here |
| --- | --- |
| Movie context `G` | `+50` float supplied to the driver; `+54` time fraction; `+58` current timeline; `+5C` consumable input code; `+60` definition table; `+1BC` period |
| Timeline `T` | `+18` resource; `+1C` active/cache list; `+5C` record cursor; `+60` current frame; `+64` Play1/Stop2; `+68` pending request; `+6C` raw flags; `+70` requested frame; `+CC` raw balance |
| Action queue `Q=T+34` | `+18` pointer array; `+1C` count, that is `T+4C/+50` |

Runtime frames are 1-based, unlike the 0-based indices of the `VmfMovie`
data. The initializer 4487F0 sets current=requested=1, pending=0,
raw flags=7; Play is 2 for FrameCount 1, otherwise 1. It does not by itself
launch the scripts of the first frame.

The native cursor targets runtime records: their `+4` is the next pointer.
It is not the DWORD size of the raw file to be reinterpreted as an address.
The starting point is `resource + DWORD[resource+14]`.

## Handler selection and effects

4418B0 reads `selector=BYTE[tag+441DA8]`, then the branch from the DWORD
table at 441D5C. Session reads 21/22 and the earlier assembly export establish:

| Tag | Selector / branch | Vtable | Slot 0 entry |
| --- | --- | --- | --- |
| 0 Timeline End | 0 / 441C78 | F3EE08 | 44B110 |
| 1 ShowFrame | 1 / 441C37 | F3ED34 | 44B000 |
| 12 Action | 7 / 441C88 | F3ED90 | 443A20 |
| 26 Placement | 11 / 441C21 | F3ED54 | 44AF80 |
| 28 Removal | 12 / 441C2C | F3EDE0 | 44AFC0 |
| 39 Timeline definition | 14 / 441C70 | F3EE44 | 44B160, not closed here |

The getters of 0/1/12 return 0/1/12 respectively: 47A8A0, 423F20,
4416F0. No handler number is derived from the SWF standard.

Placement and removal are executed immediately in record order.
44AF80 calls 446B70 then increments `G.current+CC` if its AL is non-zero;
44AFC0 calls 446FD0 then decrements that balance even if no node was
removed. Both advance the cursor and return AL 1. These are not success
returns: a placement ignored for lack of capacity can increment the
balance, an empty pool can fail to increment it. **The balance must not be
silently replaced by `children.size()`**; the passes test whether it is strictly
positive as a signed 32-bit integer.

## 448B20: publication, lookup and dispatch

Observed ABI: ECX=timeline, mode argument tested on its low byte,
RET 4. The body saves `G+58`, publishes the timeline, then:

1. current/requested equality: pending is reset to 0.
2. If pending and requested<current, signed comparison: 447200 rebuilds
   the list toward the requested frame; balance 0, current 1 and initial cursor.
   If requested=1, pending is cleared. The context is reread after the call.
3. Non-zero mode **or** pending: call of slot 0 of the current record; the
   cursor is reread on each turn, as long as the handler returns non-zero AL.
4. Otherwise, if `flags+6C &1`, traversal of the eligible active children and
   recursive call 448B20(mode 0). Otherwise no record is read.

446A60/25 bytes closes the predicate of this last pass: child `node+3C`
non-null and virtual getter +8 equal to 0. It reads no additional flags or
context. Normal exits restore `G+58`; no restoration on a foreign
exception is established.

## 448E60: ShowFrame boundary and distinct phases

44B000 loads `G.current` into ESI and delegates to 448E60; its AL controls
the continuation of the dispatch. The phases have different returns:

| Situation, in this order | Effects | AL |
| --- | --- | --- |
| Pending on entry | current++, cursor=next; clears pending if the request is reached; no child/action pass | 1 |
| Otherwise child prepass, if balance>0 | 4476C0(mode 1), then rereads pending | depends on what follows |
| Request appearing during the prepass | Rewind: 447200, balance 0/current 1/initial cursor; otherwise current++/next; pending recomputed | 0 |
| No request after the prepass | 443A60 on the parent's queue, then rereads pending | depends on what follows |
| Request appearing during the actions | Same rebuild or advance; pending recomputed | 1 |
| Still no request | Postpass 4476C0(mode 0) if balance>0; then, if Play==1, current++/next | 0 |

**Pending is not reread after the postpass** before the advance conditioned
by Play. A request coming from a child before the actions therefore does not
follow the same chronology as a request coming from the parent's actions. A
Stop does not automatically make the whole subtree inactive.

4476C0 traverses only the active nodes via `next+94`, filters the children
of kind 0 and calls 448B20 with the received mode. The next pointer is read
after the child returns. The cache is not traversed by these passes; keeping
an instance alive does not allow updating it globally.

## Deferred actions and timeline End

443A20 enqueues `record+8` at the end of the Q array **only if pending is
zero**. It always advances the cursor and returns AL 1. No quota, grow or
realloc is present in these 56 bytes; the origin/capacity of the storage
remains to be tied to its constructor.

443A60 fixes the bound `base+4*count_initial`, successively rereads the
pointers in that array and calls 443AF0 with EAX=script, ECX=Q. It resets the
count to 0 only after this loop. This establishes no stack clear. A
reentrant append does not extend this bound; replacing this behavior with
"pop until the queue is empty" would change the order and the termination.
Preservation of the IP and of the stack during a nested invocation of 443AF0
on the same Q remains to be closed.

The **record** End 44B110/69 bytes writes requested 1, calls 447200, resets
balance 0/current 1 and the initial cursor, then returns AL 1. It does not
explicitly modify Play or pending and does not explicitly clear Q. The
dispatch can therefore continue on the first frame within the same call. This
End is not the End of the action bytecode, which terminates a script.

Discriminating case to keep: a request coming from the prepass can exit with
AL 0 without calling 443A60. An automatic clearing of the queue on a Goto
must not be added by analogy with another player.

## 441DF0: cadence driver and services around the passes

The driver takes its context in ESI and returns RET 12. It publishes this
context in 10E8078/10E807C, first calls 44A4C0, then computes:

`X = float_operand / G.period + G.fraction`.

4418B0 establishes `period = float32(1 / WORD[file+3C])` after converting
this unsigned WORD. The frozen corpus gives 60 for 33 movies, 30 for the six
PC/Xbox `gauges/hud/hud_cha`. **The unit of the time operand and its caller
are not established here**; neither seconds nor milliseconds are imposed.

X is capped at 1 if `G+34==0`, otherwise at 4 (F418D4=40800000). If the
COMISS comparison returns, JBE lets the unordered case through; no lower
bound is visible. The MXCSR exception masks remain relevant.
CVTTSS2SI32 produces N, then `fraction=X-float32(N)` is stored. A value
exceeding the cap loses its integer debt and its fraction prior to the
capping; this is not an accumulator with preserved debt.

- N<=0: one 448B20(mode 0) pass, then 4473B0 and 447520.
- N>0: N repetitions of 448B20(mode 1), 4473B0, 447520 in that order.

The float field `G+50` is written once before these repetitions. The input
code `G+5C` is set to 0 if `G+BE0` is non-zero, otherwise receives its
DWORD operand. The units, the complete ABI of the three operands and the exact
roles of 44A4C0/4473B0/447520 remain open. These services are called even with
zero timeline steps: do not tie every input to the advance of a frame.
On normal exit, both globals are restored to the same saved 10E8078
pointer; the context fields are not reverted.

## Reentrancy, limits and next reads

Context pointers, cursors, requests and list links are reread after calls.
The effects of a resource 37, of destructors and of FSCommand can cross host
services; these bodies guarantee neither lifetime during a callback, nor
rollback, nor restoration on exception. A snapshot of every field around the
callbacks would invent a contract.

The [script calls](vmf_script_calls.md) batch now specifies the entry and
exit of 443AF0, the collection of Call, the capacity of the queue and the
sharing of the stack between root and children. The [C++ driver](vmf_frame_player.md)
now integrates the bounded queue, the scripts and the child passes. Call and
the services remain to be connected before a complete execution of the menus.

Remaining priorities before a complete executor:

1. Resource 37 services, text/focus and variables: first refusal in 37 of
   the 39 private VMFs. GetURL/FSCommand 83 stops both the PC and Xbox
   LOG_IN movies.
2. Connect Call 445640 to the bounded collector, then close the host callbacks
   before any claim of complete reentrancy; no stack reset added.
3. Contracts of 44A4C0, 4473B0, 447520 and callers of 441DF0: time, input,
   callback order, consumption and reentrancy.
4. Data initializations/services and rendering: the dispatch handlers of the
   17 tags read are closed, including 44B160; this does not restore their
   graphic resources, texts, sounds and interactions.

Visit/script budgets, call depth, handles/generations and refusal of
inconsistent states are explicit C++ guards, not guarantees found in these
loops. The absence of cycles in the resource graph does not prove the absence
of recursion via scripts or the host. No co-op trial, complete menu rendering
or controller behavior follows from it.
