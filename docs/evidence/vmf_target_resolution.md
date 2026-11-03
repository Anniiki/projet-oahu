# VMF: target resolution and recovery after failure

## Evidence and scope

The contract is established on the static exports of the Paradise secondary
executable, SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
The primary reference remains the official PC MC 1.66 A executable, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
This batch establishes neither an official invocation nor the execution of
Xbox movies.

The new exports are in the private batch
`reprise-2026-10-31/rea/vmf-target-resolution/session/`: resolutions 02..06,
then assembly/raw reads by pairs 09/10, 11/12, 13/14, 15/16, 17/18.
They cover respectively `443FE0/169`, `447FE0/132`, `4443E0/74`,
`444430/95` and `444090/124`: **594 bytes in five bodies**. The earlier
pseudocode exports 81, 91, 49, 50 and 80 of `vmf-action-interpreter` serve as
landmarks; the contracts below rest on the assembly.

The parent report `exact-byte-validation.json` covers 24 successful requests,
880 bytes of code and 64 of data, including label reads outside this
sub-batch. Our review `vmf-target-resolution-review/validation.json`
independently cross-checks the five ranges studied, **594 Rea bytes = dumps**,
and 33 files unchanged before/after, without opening the PE. The final
snapshot has SHA-256
`9502991c02c030d2df3ca9df21c6f74f7509ecd06a2dfe6b88035f0762e66c6a`.

## Inputs, outputs and mutations

We keep the names of [the script call study](vmf_script_calls.md): G is the
movie context; Q holds a script's context, and `G+58h` its current timeline.
These are x86 layouts, not portable pointers.

`443FE0` receives in EAX **the address of the timeline slot to modify**, a
C-string path as a stack argument, an optional depth output in ESI and an
optional list address output in EDI. It returns the boolean in **AL only**,
with `RET 4`. It does not save the slot's initial value in order to restore
it on failure.

`447FE0` receives EAX = address of the list head, EBX = destination slot, and
a `char**` on the stack. On the first match, it advances this text cursor,
writes the child into the slot and returns the depth DWORD `node+90h`. Without
a match, it writes zero into the slot, returns `FFFFFFFFh` and leaves the text
cursor unchanged. Depth zero is therefore valid, not a failure.

Before each named lookup, `443FE0` may publish the address `timeline+1Ch` to
its EDI output; after the lookup, it may publish its result to ESI, including
`FFFFFFFFh`. The root/parent steps do not fill these outputs. These outputs
and the slot can thus retain partial effects. So `A/..` can end on the parent
while keeping the outputs of the lookup of A.

## Effective path grammar

The engine does not tokenize the names beforehand:

- An empty path, or one starting with `:`, succeeds immediately without
  consulting the slot. The suffix after this `:` is not processed by this
  invocation.
- A single leading `/` climbs the `+14h` parents up to the null parent, then
  consumes that slash. There is no normalization of repeated slashes.
- At the start of a step, two dots `..` take the parent and consume exactly
  two bytes, **without requiring a separator**: `..abc` climbs then looks up
  `abc`. An isolated dot has no special handling.
- Otherwise, the lookup compares the whole stored name. After a successful
  step, a slash present at the reached position is consumed; NUL or `:`
  terminates.
- If a step sets the slot to zero, resolution fails immediately, without
  returning to the previously reached ancestor. `..` above the root therefore
  leaves a null slot. A trailing slash after a match is accepted.

A name containing `/` or `:` can win as a literal name: with the children
`a/b` and `a`, the list order determines which one can consume `a/b` first.
A later failure under `a` does not come back to try the literal name `a/b`.
A second slash can trigger the lookup of a child with an empty name. Replacing
these rules with a standard split into segments would change the contract.

## List, comparison and name length

`443FE0` passes the **active list `timeline+1Ch`** to the lookup, never the
cache heads `+10h/+14h`. `447FE0` follows `node.next+94h`, skips null children
and those whose virtual getter `+08h` does not return zero. It retains the
first match in that order; duplicate names are not deduplicated.

The comparison requires `((query_byte XOR stored_byte) & DFh) == 0`
for each byte of the stored name. At its end, the
next query byte must be NUL, `:` or `/`. This mask also accepts certain pairs
of punctuation and high bytes separated by bit `20h`; it is neither `_stricmp`
nor a Unicode casefold.

The placement `446F24..446F3F`, already exported, copies 32 bytes then forces
the last one to zero: **31 useful bytes per node name**. This limit does not
truncate the direct SetTarget path. The corpus contains 50 direct operands
longer than 31 bytes, maximum 39; they are not 50 failures for all that, since
a path can traverse several names. TellTarget uses `44B250`; a truncation
originating from a text Push is distinct from the lookup itself.

## SetTarget, TellTarget and IP recovery

The dispatcher calls SetTarget `4443E0` with EBX = Q and EAX = direct text,
and TellTarget `444430` with EBX = Q. **Each call first resets the current
timeline to `Q+14h`**, the timeline saved at script entry. A relative target
is therefore not automatically relative to the last SetTarget. TellTarget
performs this reset before popping the text through `44B250`. Neither restores
the popped text or the previous target after a failure; both then pass Q to
`444090`.

This skip sets `Q.autoAdvance+10h` to zero and advances **at least the current
action**. It updates the physical IP, the ordinal and the compact position at
each step: u16 opcode below `80h`, respectively `+2/+1/+1`; otherwise u16
length (`longueur`), `+4+align2(longueur)`, `+1`, `+3+longueur`. It stops
**before** the next End `00h`, SetTarget `8Bh` or TellTarget `20h`, without
executing the skipped actions. The interpreter thus avoids a double advance
and can execute the next target selection, which starts again from Q+14h. The
slot stays in its failed state during the skip.

## Guards required by the port

The native bodies impose neither string or script bounds, nor a traversal
quota, nor cycle detection or a lifetime guarantee during a virtual call. The
future port must use the actions already validated by VmfMovie, verified
instance identities and explicit budgets for characters, nodes/parents
traversed and actions advanced. The dispatch already debited and the first
advance of the skip must not charge the same action twice.

The DFh mask can consider a stored space equal to the query's NUL, then read
past its terminator. A comparison running past the bounded C-string must be an
**explicit modern error**, without inventing residual bytes. Overflow, budget
exhaustion, a target that has become invalid or reentrancy also require a
refusal contract; these guards are not native behaviors already proven. The
ordinary partial failures described above must not be silently turned into a
rollback.
