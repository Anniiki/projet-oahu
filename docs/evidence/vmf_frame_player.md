# C++ driver for VMF frames and scripts

## Result and scope

`VmfFramePlayer` hooks up the live lists, the placements/removals and the
script runner to an explicit invocation of `448B20(mode0/1)`. It keeps the
interrupted steps between calls to `resume`, including inside a child
prepass or a script. It owns its display, its shared stack, its bounded
queues and its cursors; no per-frame stack reset is added.

The time driver `441DF0`, its three services, Call, focus, rendering and
input remain to be integrated. Reading the Xbox resources uses their own
formats with this secondary PC semantics; it proves neither the equivalence
of the XEX nor complete navigation with the controller.

`VmfFramePlayerLimits::variables` now enables the dictionary owned by the
display, Get/SetVariable and the
ordered scope cleanup. Leaving this option out
keeps the previous boundaries. The separate option `text_bindings`
enables the PC text hookup and implies the
dictionary. Fields requiring focus remain missing services; no displayed
interface is inferred from this support.

The `properties` option adds the native properties setter.
It uses the already bound dictionary if there is one and does not enable it
implicitly. The earlier options and inputs keep their boundaries; `GetProperty`
and the external commands remain explicit.

## Evidence and new handlers

The native phases and the queue are
the previous references. The new private batch `rea/vmf-frame-player/`
concerns the Paradise secondary, SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
Rea/PE/DUMPBIN agree on **512 bytes of code windows**; 392 bytes of vtables,
the previous tables and the assembly listing of `4418B0` close the dispatch
of the 17 types accepted by the reader.

Ghidra does not recognise several of these entries as functions. The first
assembly request failed after a negative resolution; it is preserved.
The second session uses bounded reads and closes after 11 successful
requests. The function extents are therefore not attributed to the provider.
The windows include padding and neighbouring instructions; their 512 bytes
are not presented as 512 bytes of newly ported bodies.

| Types | Established handler / dispatch effect |
| --- | --- |
| 2, 7, 11, 14, 20, 37, 46 | `44B3D0`: global registry, DWORD ID, then next |
| 10 | `44B010`: same registry, WORD ID, then next |
| 39 | `44B160`: registry, then skip of the sprite's extent |
| 9 | `44B070`: copy of the raw DWORD to `G+1B8`, then next |
| 43 | `44B180`: next only |
| 15 | `44B3B0`: reads with no published effect, then next in this secondary |
| 0, 1, 12, 26, 28 | End, ShowFrame, queue, placement, removal already documented |

`44ACB0` initialises the registry to null. The port stores in it indices of
records owned by the immutable program, without x86 pointers. Registration
happens during dispatch, even during a frame request; the rewind does not
clear this registry. A placement before its resource is registered is an
explicit modern error before mutation, not an access to a null pointer.
The data initialisations for text, images and sound are not considered
complete through this closing of the dispatch handlers alone.

## Preserved chronology

ShowFrame with pending advances immediately and continues the dispatch.
Otherwise, the child prepass (mode 1), the parent's drain and the child
postpass (mode 0) remain distinct. A request coming from the prepass adjusts
the cursor then returns AL0 without draining the queue; a request coming from
the actions returns AL1 and can process the next frame immediately. After the
postpass, pending is not read again before the advance conditioned by Play.

The balance is tested as a signed 32-bit integer for both ShowFrame passes.
The mode 0/flags 1 pass ignores this balance and walks the active children.
The next link is read after the child returns; the cache is excluded.

A step back to 1 clears pending before the mode/pending choice. **In mode 0**,
it can therefore reset the cursor and the lists to their start without
executing the records of frame 1; a following mode 1 invocation rebuilds them.
A timeline End writes requested 1 before rewind, then resumes at the first
record within the same invocation. Neither Play nor the queue is cleared by
analogy.

The drain captures its initial count and reads each slot when visiting it.
Additions beyond this bound, including through `enqueue_script` between
resumptions, are forgotten at the final count 0. No drain-until-stable or
recursive Call interpreter is substituted for the native contract.

## Usage and modern guards

Create the player from a `VmfProgram`, then `begin(owner, advance)` and
`resume(budget)`. The record/node/script/action budgets are shared by the
entire hierarchy during each resumption. The instruction and scan limits as
well as the host boundaries keep the continuation. Text, capacity, identity
or service errors keep the prefix and fault the player. An invalid input
budget can be corrected before a new resumption.

The player owns current and the cursor. Use the display's Goto/Play/Stop
commands for requests, without modifying current manually. Serialise the
accesses; during a suspended invocation, only explicit queueing is allowed
among external mutations. No reentrancy or general restoration of state
after a callback is promised.

The queues and the dispatch stack are preallocated at creation. The capacities
are modern settings, distinct from the unknown native value `G+14`.
The test forbids ordinary allocations during begin/resume/enqueue; this does
not constitute a measurement of smoothness or of frame budget. The visit
quota for paths remains per lookup and its cost can accumulate.

The Windows diagnostic opens a VMF read-only and emits a JSON summary:

```powershell
.\build\Release\tdur_inspect_vmf_playback.exe "C:\copie-privee\map.vmf" pc
```

It bounds the resumptions and the invocations, keeps the host stops and
launches no commercial executable. Exit code 2 describes an error or a
missing service of the player; `needs_host` is an observation, not a
navigation success.

To correlate a stop with the bytes of the file, add --source-locations.
When the report contains a script cursor, the JSON exposes source_offset
(action), script_source_offset, script_source_size and record_source_offset.
These positions come from the validated ranges of the VMF owned by the
program; they do not use the order of an external inventory. The option
neither consumes nor acknowledges the host command. Without this option, the
summary keeps its format.

The `--host-operands` option adds the two operands of the GetURL `83` currently
pending in `host_operands`. It reads the validated ranges of the owned film,
without advancing the script, calling a receiver or acknowledging the command.
The values remain bytes: non-ASCII or special characters are escaped as
`\u00XX`, without assuming a UTF-8 string. Dynamic GetURL2 is not covered.
Without this option, the JSON stays unchanged. A creation error also exposes
the code and the detail of the nested display refusal on stderr.

The targeted x64/Win32 diagnostics on unchanged private copies find
`FSCommand:sbdd` with an empty second operand: PC map at offset 3848, PC
`LOG_IN` at offset 2052, from its very first action. The PC text bindings mode
explicitly refuses the Xbox donor profile; this refusal is not a successful
navigation. The six asset comparisons preserve the earlier JSON outputs and
codes, two profile diagnostics agree and two repetitions of the new flag are
rejected. The private evidence is in `vmf-current-host-boundaries-v1/`.
These observations target the next host hookup, without running the
commercial game or an online connection.

## Validation and next hookup

The phase oracle was written before the new CPP. The harness and the
production code have the same author: **no independence is claimed**.
The first builds were refused because of signed literals in the tests, then
the initial fixture was refused because of a definition counter at zero:
outputs and versions preserved, type/envelope corrections without changing
the phase expectations. No production defect was discovered by these first runs.

**706 checks per architecture**, 22 fixtures included, pass on x64 and
Win32. The 64 x64 executables are built explicitly; CTest passes
**66/66 x64** and **5/5 targeted Win32**. The cases cover parent/children
order, a request in each phase, End, cache, registry, negative balance,
bounded queue, budgets and resumption without replay, modern refusals and
absence of ordinary allocation.

The initial diagnostic on the 39 unchanged private VMF gives the same results
per file on both architectures: **37 explicit refusals for text/focus
services**, **2 stops on opcode 83 in PC/Xbox `LOG_IN`**. No complete
invocation of this corpus is obtained. This evidence designates the services
for resource 37 and the login GetURL/FSCommand as the next hookups, with
variables and Call afterwards. It proves neither a displayed menu nor multiplayer.
