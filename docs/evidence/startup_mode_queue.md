# Mode work queue: 9B4400 and 9B4710

`startup_mode_queue.hpp` supplies removal and dispatch for the mode work
queue. `ModeQueueBinding` connects the real dispatch body to the existing
`9B4BA0` drain in [world synchronization](startup_world_sync.md).

## Evidence

Primary source: the official PC MC 1.66A executable, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The two retained atlas exports identify that image. Read-only disassembly
windows `9B4400..9B4487` (136 bytes) and `9B4710..9B4774` (101 bytes)
match its bytes; the image hash is unchanged before and after collection.
No game process or new analysis session is needed for this validation.

## Shared state

The same `GameModeRuntime` owns current work `+200`, completion byte `+208`,
the 65 known DWORD positions `+210..+310`, and signed count `+314`. Modern
slots hold shared references rather than 32-bit process pointers. Each work
object owns its `+04` queued byte. Pointer identity remains distinct from
the byte value; duplicates are allowed and compared by object identity.
Default C++ values do not assert native constructor behavior.

The 65 positions describe the available storage before the count, not an
established native enqueue capacity. Dispatch and removal do not construct
or enqueue work. A positive count larger than that storage is refused.
Removal of a found item at count 65 is also refused: the extra source word
would be the count at `+314`, not a valid host work reference. A not-found
search at count 65 still returns normally. These are explicit modern bounds.

## Removal, 9B4400

EBX identifies the owner; the stack argument identifies the work. The body
enters `606B60` on member `+318`, then reads the signed count. Nonpositive
counts and unsuccessful searches leave through `606B90` and return AL=0.
The first matching slot wins, even when several slots contain the same
work. There is no callback during the search or move; concurrent mutation
outside the gate is not a supported host contract.

At `9B443E`, ECX becomes count minus found index. `REP MOVSD` at `9B4452`
copies that many words from the following slot. Thus the final copied word
comes from immediately after the active range. It is not replaced with a
fabricated zero, and its source slot is left unchanged. The count is then
decremented, the argument object's `+04` byte is cleared, the gate is left,
and AL=1 returns. The current-work field is not cleared by removal.

The argument is pinned throughout, including the leave callback, even when
the shift removes the last queue reference. A missing object at the queued
byte write is a modern refusal after the shift and count decrement. Gate
entry, invalid extent, missing object and gate exit refusals preserve their
completed prefix; a refused path does not invent a native leave. Host
recovery owns any gate already entered.

## Dispatch, 9B4710

ESI identifies the original owner. The signed count is read before the
completion byte. Completion must equal exactly 1; other values do nothing.

For positive count, the first slot is copied to `+200`, completion is cleared
and `606D40` is called on member `+1C8`. Its native boolean result is ignored.
After the signal, instruction `9B4745` reloads the original owner's `+200`.
Instruction `9B474B` then loads the removal owner from global `10E7238` into
EBX. Removal therefore can act on a different work object and a different
owner. Both live reads are preserved. A returned removal with AL=0 still
returns normally to the dispatcher.

For nonpositive count with completion 1, current work is cleared, completion
is cleared, and the signal is the tail call. This branch does not read the
global owner or invoke removal. Modern refusal of signal, missing live
owner or refused removal remains distinct from a returned native false.
Owners must remain alive across host callbacks.

## Composition and validation

The binding forwards gate entry/exit and the captured yield provider and
supplies the reconstructed `9B4710` to `9B4BA0`. Its composition test drains
three actual queued objects, checks single-work and low-byte-zero modes,
and observes nested gate depth two. The host gate must support this recursive
entry. No host mutex, semaphore or worker thread is installed by this module.

Targeted tests cover every removal position at counts 1, 2, 4, 63 and 64,
all 65 retained slots, duplicate identities, the extra source slot, signed
count boundaries, null work after prefix effects, argument lifetime, all
refusals, exact completion value, and replacement of both work and owner
during signal. Run `startup_mode_queue` together with the seven previous
world, resource, preparation, cycle and mode suites in x64 Release /W4 /WX.

Still required: actual signal `606D40`, completion callbacks, gate bodies
`606B60/606B90`, yield provider `9EC158`, `609A20`, work construction/enqueue,
and complete host installation with synchronized workers. Addresses below
`6DD000` require their own official corroboration before integration.
This work does not establish a loaded or playable world.
