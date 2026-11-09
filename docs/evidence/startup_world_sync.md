# Loading queue pumping and mode completion

`startup_world_sync.hpp` reconstructs `9A2A20`, `9A2BD0` and `9B4BA0`.
`WorldSyncBinding` installs these bodies into the existing `9A4930` caller
for the owned loading service identified by `10E80C0`. Required leaf operations
remain explicit. No native instruction, address or operating-system lock is
invoked by these bodies.

## Evidence and shared state

Reference: official PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Existing atlas exports and bounded official disassembly establish:

| Window | Compared bytes |
| --- | ---: |
| `9A2A20..9A2A8D` | 110 |
| `9A2BD0..9A2CF4`, including alignment | 293 |
| `9B4BA0..9B4C3B` | 156 |

All 559 bytes match the unchanged image. The fourth window verifies the
on-disk word `B438D6` at `9EC158`. This word is an RVA of retained hint/name
data: at VA `F438D6`, hint `0347` precedes the NUL-terminated name `Sleep`.
It is not an executable function address. The active PE IAT directory does
not cover `9EC158`; these static bytes do not establish its runtime resolver.
The caller captures the live dispatch slot; a host implementation remains
required. Three atlas exports are retained.
No game or new analysis session was opened.

The loading service owns 32 records, native base `+1C` and stride `538`
hexadecimal. Its per-record pending and settled bytes at `+414` and `+528`
are therefore service-relative `+430` and `+544`. The query and pump access
the same owned bytes. The completion callback is record `+524`.

Current selection `+56590` is a stable slot index, an independently owned
external record, or no record. Copying or moving a queue with an indexed
selection does not leave an internal pointer into the old queue. Callback
and external-record references are pinned for the duration of a call.

## Query and pump

`9A2A20` returns AL=1 if any of the 32 entries has a nonzero pending byte
and a zero settled byte, otherwise zero. The native loop is unrolled four
entries at a time; it does not reset an already positive result.

When active byte `+56594` is zero, `9A2BD0` compares the current pointer with
the 32 record identities. **Every match sets EAX to zero**, regardless of
the matching index (`9A2C62..9A2C69`). Since the following scan increments
before testing, the order is `1..31,0` after any match. With no match the
order is `0..31`. This is not a round-robin scan after the current entry.

The first pending, unsettled record sets active to one, finished byte
`+56595` to zero and current selection to that record, before `606D40`:
ECX identifies service member `+56558`, EAX the selected record. A returned
signal ends the pump. No eligible record leaves all state unchanged.

When active is nonzero but finished is zero, the pump returns without
calling anything. When both are nonzero, it clears active, finished and
byte `+572E4` before resolving the current record. A nonzero settled byte
skips the callback. Otherwise a nonnull callback receives the current record,
a notice whose initialized DWORD is four, and count one. Only that initialized
word is exposed; the other native stack bytes are not invented as zeros.
The body neither clears the pending byte nor marks the record settled.
Callback changes to the selection, callback slot or flags remain visible.

Invalid indexed selections and missing completed records cause modern
refusal. In the latter case the three earlier byte clears remain. A signal
refusal retains the selected record and flags already written. A callback
refusal does not turn into a native completion.

## Mode completion

`9B4BA0` uses the actual `GameModeRuntime` supplied by its caller. That object
owns signed count `+314` and byte `+208`; no second count or completion flag
is stored in the binding. The host supplies their actual initial state.

The body calls `606B60` with ECX identifying owner member `+318`. It then
captures the count and the yield function from `9EC158`. Only the low byte
of its stack argument controls the branch: `100` hexadecimal behaves as zero.

For a positive initial count, the stop threshold is zero when the argument's
low byte is zero, otherwise initial count minus one. `9B4710` receives the
owner in ESI until the live signed count is at or below that captured
threshold. The count is re-read after every returned call. The body does not
decrement it itself. Callback increases, decreases or negative results remain
observable; a nonzero argument does not guarantee exactly one callback.

After this positive-count branch, completion must equal **one**, not merely
be nonzero. While it differs, the captured yield function is called with
argument zero. A zero low-byte argument also performs the final completion
wait when the initial count was nonpositive. A nonzero low-byte argument
with a nonpositive initial count skips waiting. Normal return calls `606B90`
with the same owner member `+318`.

Host-specified budgets limit process and yield calls. Exhaustion or a refused
port preserves prior effects and does not manufacture a successful completion
or native gate leave. If a host installed a real gate, it owns recovery after
such a refusal. The body does not silently install a lock, scheduler or
successful callback. Dispatch lookup is a read, not a mutating extra callback.

## Binding, validation and limits

The binding rejects unknown loading-service identities. It forwards `609A20`
to a required host port; the subsequent mode-owner lookup remains in the
existing `9A4930` body. A composition test changes that owner during `609A20`
and confirms that `9B4BA0` uses the new object.

Tests cover all pending/settled byte combinations at every slot, selection
order for every current index and competing candidate, wrap to slot zero,
copy/move identity, external record lifetime, callback removal, prefix effects
on refusals, signed counts, low-byte arguments, exact completion values,
captured dispatch and thresholds, both budgets and the real `9A4930` chain.
Seven CTest programs include the prior sector, area, loader, preparation,
cycle and mode suites. Build `startup_world_sync_test`; select
`startup_world_sync` and those six prior suites. Validation uses x64 Release,
`/W4 /WX`, BelowNormal priority, one job, the shared build mutex and 6 GiB guard.

The [mode work queue](startup_mode_queue.md) now supplies `9B4710` and its
removal dependency `9B4400`, with live work and owner reads after signal.
Still required: signal `606D40`, callback implementations, gate operations
`606B60/606B90`, the yield provider and `609A20`, together
with complete host installation and loading operations. The present model
supports callback-visible state on the owning thread; unsynchronized mutation
from another thread is unsupported. It does not yet install the game's worker
threads or prove their concurrency. No loaded world, rendering, playable
client or two-client session is established.

Related: [world loading callers](startup_world_loaders.md),
[sector resources](startup_sector_resource.md).
