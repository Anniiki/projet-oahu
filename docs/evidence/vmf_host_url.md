# GetURL and GetURL2 host bridge

## Origin and scope

`execute_vmf_actions_with_host` and adapter `VmfFsHostActions` connect GetURL `83`
and GetURL2 `9A` actions of the [executor](vmf_execution.md) and [frame
player](vmf_frame_player.md) to [FSCommand envelope](vmf_fscommand.md) `445CB0`.
The primary reference remains the official PC MC 1.66 A, SHA256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`. The
routines below are statically established in secondary
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`, at the
indicated proof level.

| Routine | Retained behavior | Proof level |
| --- | --- | --- |
| `443AF0/760` | Dispatcher: case `83` to `445CB0`, synchronous call; then `443A90` if `Q+10` is 1, then next opcode in the same loop | [PC dispatch](pc_vmf_dispatch.md) |
| `443AF0/760` | Case `9A` to `445D40`, synchronous call, same continuation | Case label read in Ghidra pseudocode only; jump table not disassembled |
| `445CB0/144` | Envelope on the literal URL, passed alone in EAX; target is not transmitted | [PC dispatch](pc_vmf_dispatch.md), [envelope](vmf_fscommand.md) |
| `445D40/162` | Two `44B250` on context stack, target discarded then URL, then `445CB0` continuation | DUMPBIN listings reread for this batch |
| `443A90/83` | One-action advance | [PC dispatch](pc_vmf_dispatch.md) |
| `44B250/104` | Text pop; reading top precedes count test | [Action contracts](vmf_action_semantics.md) |

These five bodies are classified `official_memory_equal` in the
global primary window table: their bytes are present in official
process memory, without execution trace. Secondary bodies SHA256:
`445CB0` `7888c9e0ac885ecbdaca9e08bed002c8440e594651576759de728bdc11fd875a`,
`445D40` `626fce0ee3b5b74ffce40a6a2f95eef99f293f6cebcc866302f07124f6894d3b`.

Case `83` to `445CB0` and common advance are established in [PC
dispatch](pc_vmf_dispatch.md), evidence
`74fec9dcbae64fe9e3fd8c5dd4163880c2dddda8e82b6bb27cc1c3c6a020e4b2`. This
document does not close other handlers. Case `9A` to `445D40` has only one
source: private static analysis export
`ui-map-input-dispatch/session/29-pseudocode.json`,
of same evidence, Ghidra pseudocode of `443AF0` whose jump table was not
disassembled. Assembly listings take precedence over pseudocode ([action
contracts](vmf_action_semantics.md)): this case remains to be confirmed by a
listing of `443AF0`. Semantics of `445D40` ("the second pops target then URL")
comes from [action contracts](vmf_action_semantics.md) and private DUMPBIN
listings `vmf-host-callback-v1/dumpbin-445d40-445de1.txt`
and `dumpbin-445cb0-445d3f.txt`, reread for this batch; see also [host
commands](vmf_host_commands.md). Current stops:
`reprise-2026-10-31/vmf-current-host-boundaries-v1/`. This batch required no
new access to the game, process or executable.

The listing of `445D40` reloads the context stack field before each of its two
calls to `44B250`. The first result is overwritten; only the second becomes the
URL compared over ten bytes to prefix `FSCommand:`. Neither payload nor action
flags byte are read. The continuation reproduces `445CB0`: loads of `10E8078`,
`4374F0` if `+30` is strictly positive, `606B90` and `606B60` on `114C050`
according to `+35`, userdata `+9CC` then callback `+9C8`, finally both globals
receive the saved owner.

## Order of effects

For GetURL `83`, the executor follows this order:

1. In target skipping phase, the action is skipped without any host call.
   Instruction budget is checked as before.
2. Without a host, or if the host declines the opcode, the current `needs_host`
   stop is preserved and nothing is consumed.
3. Modern guards on both literals and the null byte following the URL, then
   slot reservation. Exhausted capacity returns `needs_host`, resumable.
4. The URL is charged to the text budget; target is never charged.
5. The `awaiting_host` phase and ticket are published before `begin_url`.
6. The envelope executes prefix, log, gate, userdata, callback, optional
   `enter` and restoration of both globals.
7. Only upon actual completion: `execute` phase, ticket cleared, one instruction
   counted, cursor on next action in the same invocation.

For GetURL2 `9A`, reservation precedes any pop. Target then URL are popped; the
URL window is resolved after both pops, as for StringAdd, because a numeric URL
reuses the shared target buffer. Final NUL of the window is checked, URL
charged, then steps 5 to 7 apply. A URL without the exact prefix still consumes
both values and finishes without access to globals.

An unknown dependency or waiting handler frame returns `host_pending`. The
cursor and current target remain on the GetURL; the next resumption, with the
same host, replays neither pops, log, `leave`, nor the beginning of callback.

## Modern contracts and limits

These choices have no claimed native equivalent: reservation and capacity, wait
with ticket (`VmfScriptSnapshot::host_call`), host step budget per invocation
(`VmfExecutionLimits::host_steps`, 4096 by default), single URL charging,
explicit rejections on empty stack, sprite or window without final NUL.
Cancellation (`cancel_vmf_host_action`, `VmfFramePlayer::cancel_host_action`,
player fault or destruction) is terminal: it preserves the prefix, releases the
frame once and calls neither `enter` nor restoration. During destruction
cancellation, callbacks to the player are rejected as during a resumption. Any
fault published on a context bearing a ticket first cancels its slot, including
host errors: the executor then sends a defensive cancellation whose response it
ignores. A host that already freed the slot rejects it by generation; a host
that kept it frees it. Rejected without state change: entry on an in-progress
context, waiting context without host, ticket rejected by host, backend outside
enumeration.

Contract of `VmfHostActions`: an error from `begin_url` or `resume` frees the
ticket and preserves executed effects, except rejections without change: expired
or foreign ticket (`invalid_ticket`), call during slot execution (`reentry`),
closing host (`closing`), ticket already started for `begin_url` or not yet
started for `resume` (`invalid_request`). For `resume`, `invalid_ticket` and
`reentry` leave the ticket with the context, without fault. `VmfFsHostActions`
applies this contract.

`VmfFsHostActions` preallocates 1 to 32 slots. Capacity is a modern choice. 1,
the default value, keeps at most one in-flight call, like the native synchronous
dispatcher, without reproducing native mechanics. Capacities 2 to 32 have no
native equivalent: in-flight calls on the same globals interleave their saves
and restorations of `10E8078` and `10E807C`, and the last finished call writes
its own saved owner to both. Capacity of `VmfFsDefaultCallback` (8 by default)
must be at least that of the adapter: otherwise "Router pool is full" faults the
envelope at `callback_begin`, after pops of `9A`, log and `leave`, without
`enter`. A ticket carries the adapter's serial number
and a generation changed at each release. Inputs, owners, callback, log, gate
and handlers remain borrowed, stable and serialized. Without a bound host, the
six existing entry points, player and diagnostic tool keep their behavior.

Limits:

- Production owner `10E8078` and its fields `+30`, `+35`, `+9C8`, `+9CC`, log
  `4374F0`, gate `606B90`/`606B60` and `film+AC0` receivers, including `sbdd`
  and `zoed`, are not wired. No production code binds a host today: outside
  `src/ui`, the only consumer of `VmfFramePlayer` is `tdur_inspect_vmf_playback`,
  which does not bind one. Without a bound host (this tool and all existing
  code), the `needs_host` stop is unchanged: PC `LOG_IN` stops at action 0,
  offset 2052, identical before and after this batch. With a bound
  `VmfFsHostActions` and unknown production inputs, the call waits in
  `host_pending`; only tests exercise this case. This batch delivers the bridge,
  not the connection; neither navigation, authentication nor co-op is
  claimed.
- `445D40` and case `9A`: secondary semantics and corroborated presence, no
  observed execution; switching `9A` to `445D40` relies only on pseudocode.
- Advance after every finished callback is a hypothesis: that a callback could
  suppress advance via `Q+10` is not established.
- A callback closing the movie or destroying script origin has no established
  native effect. The port relies only on program and origin checks at each
  resumption, which fault the context and abandon an in-flight call; no test
  exercises it.
- A bound `9A` URL (type 2), rewritten during callback, is reread as it became;
  equivalence with native in-place rewriting is not established.
- Native code is synchronous; timing driver `441DF0` is not reconstructed and
  waiting remains cooperative.
- Xbox data is read with secondary PC semantics; XEX is not imported.
- If a foreign host is presented with an input fault (invalid limits, other
  program), it is the one receiving cancellation: the real host's slot remains
  reserved until its destruction.
- With a host, the PC map is verified only by the private diagnostic below,
  with a synthetic receiver; in CTest, only synthetic movies are bound.

## Validation

`vmf_host_url_test` passes 3284 checks in x64, in PC and Xbox profiles:
scenarios E1 to E13 for executor with dummy host, H1 to H9 for adapter with real
envelope, `VmfFsDefaultCallback` and router, P1 to P16 for player. Handlers are
synthetic and write through real setters; they reproduce no native `sbdd`
service. A probe counts no `operator new` on routes `83`, `9A`, wait then finish
and cancellation, without proving universal absence of allocation or smoothness.

After further reviews, H8 presents an adapter's live ticket to another
adapter whose slot of same index and generation is in flight, then `begin_url`
on an already in-flight ticket: rejection without change of slot, envelope, log
or gate. H9 holds two in-flight slots, returns `needs_host` at capacity reached,
shows interleaving of globals and fault of a callback with insufficient
capacity. P13 faults the player during waiting: a single `cancel`, with
published ticket. P14 binds a host with properties backend: SetProperty `23`
executes after GetURL. P15 bounds host steps of a resumption, in one script then
over two scripts drained in the same resumption. P16 calls back the player during
its destruction: `invalid_state` rejection. E8 covers defensive cancellation, a
host keeping its slot after an error and failing `9A` after its pops; P7
verifies exact log, preserved `leave` and no manufactured `enter` or
restoration.

Production mutations, criterion set before measurement (each mutant must make
`vmf_host_url` fail): ten out of ten detected. Manufactured `enter` after
envelope error, by H9; serial number ignored and in-flight `begin_url` guard
removed, by H8; `9A` URL repushed and return to simple ticket clearing after
`begin_url` or after `resume`, by E8; cancellation removed from `Storage::fail`,
by P13; backend forced to `owned_variables`, by P14; host budget not deducted,
by P15; `active` not armed upon destruction, by P16. The nine mutants already
detected by review of the first version (V01, V03 to V06, V09 to V12; V10 and
V12 reworded on corrected code) remain detected.

CTest x64: 17 tests out of 17 for the batch selection (execution, variables,
properties, text bindings, player, FSCommand, router, global, collection,
bridge); 126 registered tests against 125 on the baseline, but only the 13
batch targets are built: full x64 CTest is not run. Format: modified lines
comply with clang-format 19.1.5, clean `git diff --check`.

A/B measurement of `tdur_inspect_vmf_playback`, base `0d5bb79` against this
corrected batch, x64: 39 private copies, 8 option sets and 2 invalid usages,
totaling 314 byte-for-byte identical pairs (stdout, stderr, code). SHA256 of
copies are unchanged; witnesses `pc_LOG_IN` (action 0, offset 2052,
`FSCommand:sbdd`) and PC map (action 222, offset 3848) are present on both
sides.

Optional private diagnostic `--private-vmf`, outside CTest, aligned with the
most complete options that `tdur_inspect_vmf_playback` accepts for each
profile: in PC, data and text bindings, variables and properties; in Xbox,
variables and properties, with PC text bindings rejecting the donor profile. The
bound player uses the real global handler `99B7A0` and a synthetic movie
receiver finishing without effect. Identical SHA256 before and after:
`pc_LOG_IN` `aeac4cb4…191ab`, `xbox_LOG_IN` `8ee23177…1ebe6`, `pc_map`
`42c80a82…e140`.

| Copy | Stop without host | With bound host |
| --- | --- | --- |
| `pc_LOG_IN` | `needs_host`, script 0, action 0, offset 2052, `FSCommand:sbdd`, 0 instructions | `sbdd` passes through the envelope; 16 complete invocations without new stop |
| `xbox_LOG_IN` | `needs_host`, script 0, action 0, offset 4104, `FSCommand:sbdd` | `sbdd` passes through the envelope; after one invocation, existing rejection of text and focus services |
| `pc_map` | `needs_host`, script 0, action 222, offset 3848, `FSCommand:sbdd`, 162 instructions | `sbdd` passes through the envelope; after 9 invocations, `needs_host` on GotoFrame2 `9F`, script 564, action 22, offset 558402 |

These stops describe the reconstructed player with an effectless receiver: this
is neither real acknowledgment of `sbdd`, nor navigation, nor game behavior.

Not done in this batch: full x64 CTest, Win32 build and tests, Win32 A/B. Nothing
is validated in game.

## Corrections to the first version

The first version asserted that `LOG_IN` "remains in host_pending in
production": an unmeasured claim, replaced by the first limit above. The first
version of this page also attributed case `9A` to `445D40` to the
[PC dispatch](pc_vmf_dispatch.md), which does not close it.
