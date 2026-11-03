# PC interface: properties, VMF scripts and host commands

## Reference and scope

Rea/Ghidra 12.2 analysis of the **local secondary**, SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
The official PC MC 1.66 A executable remains the primary reference; its earlier
captures do not transitively corroborate the new bodies below.
This static study launches no game and does not prove their execution.

[Official trial 25](official_trial_25.md), later than this study, now
corroborates the bodies `442890`, `443AF0`, `443A90`, `443EF0`, `445560`, `4455B0`
and `445CB0` in official memory. The setter `440BA0` and its helpers remain
secondary. The 84-byte WhatZoom body is identical in the two
[compared on-disk executables](vmf_primary_file_comparison.md), without
promotion of its jump table or of its callees.

Single private session: `<private-evidence>/reprise-2026-10-30/rea/ui-map-input-dispatch/session/`.
The JSON files preserve identity, Evidence IDs and provider limits.
The study uses the previous snapshot and targeted queries; no OpenTDU engine
code is imported.

## Contracts established on the secondary

| Routine / bytes | Static contract |
| --- | --- |
| `442890/161` | Compares `VMF\0`, checks the CRC of the first 68 bytes through `4426F0`, then requires major 2 at `+05` and minor 8 as a u16 at `+06`. The context holds the buffer at `+1C0`. |
| `443AF0/760` | Action dispatcher: u16 opcode, End 0, short and long handlers, then conditional advance. `83→445CB0`, `96→444510`, `99→445560`, `9D→4455B0`. The other handlers are not closed here. |
| `443A90/83` | Physical advance of 2 bytes for a short opcode, of `4+align2(payload_size)` for a long one; logical advance of 1 or `3+payload_size`. The ordinal counter increases separately. |
| `445560/66` | Jump: advances first, adds the signed 16-bit delta to the logical counter, then calls `443EF0`. |
| `4455B0/142` | If: pops a value from the stack and, when its numeric conversion is non-zero, performs that same branch. The general conversion `442530` remains open. |
| `443EF0/157` | Restarts from the beginning of the script and rescans until End or until the logical counter is greater than or equal to the target. Disables the dispatcher's automatic advance. |
| `445CB0/144` | Accepts the exact 10-byte `FSCommand:` prefix, then passes the suffix and a userdata to the context callback. |
| `440BA0/163` | Writes an integer property: handle through EAX whose first DWORD designates the context, decimal conversion, lookup `4491A0`, write `443770`, flag `+6C\|200h`. Helpers not reconstructed. |
| `8DC790/84` | For the state `[this+240h]` in 1..5, publishes `/:WhatZoom = 6−state`; otherwise writes nothing. The table of five destinations was read separately. |

These facts connect the [resource grammar](vmf_format_inventory.md) to the
native model. The logical counter of the branches is indeed distinct from the
physical VMF pointer. The future C++ reader requires an exact, bounded target;
the native scan can reach a boundary after the target on malformed input.
This added check must remain an explicit safety difference.

The PC checker expects 2.8; the observed Xbox resources carry 2.5. This
justifies two reading profiles, without claiming any analysis of the XEX code.
The Xbox CRC normalization is corroborated by the 39 resources, not by a
decompiled Xbox loader.

## Gateway and context state

`445CB0` receives the string through EAX. If the prefix matches, it uses the
context designated by `10E8078`, calls the function at `context+9C8` with
`(suffix, userdata at +9CC)`, then restores `10E8078` and `10E807C`.
The flag `+35` brackets the callback with `606B90/606B60`. Their nature and
their concurrency contract are not attributed here. Neither nullability nor
error handling of the callback is invented.

The integer setter temporarily changes these two globals, performs its lookup
and restores them. An x64 reconstruction will have to represent the contexts
and their lifetime explicitly; these DWORDs and x86 register conventions do
not become reusable pointers or signatures.

## Map, mouse and causality limits

`/:WhatMouseButton` at `F07CD0` has a reference from the PUSH instruction
`99C918`, followed by a call to `440BA0`. Ghidra does not attach this
instruction to any procedure; its decoded presence does not provide a complete extent.
The neighborhood notably reads `16992CA`, without sufficient proof to name
the physical buttons. The full meaning of the mouse handling remains open.

The PC and Xbox map scripts have distinct zoom bounds and conditions,
established in the VMF inventory. The native producers of Xbox events,
the focus owner, the map callbacks and the effects in a loaded world remain
to be reconstructed. Glyphs or variable names alone do not validate gamepad
navigation.

## Evidence and next step

Key files: `11/15/17` for WhatZoom, `12/16` for FSCommand,
`20/21` for the setter, `25/29` for the dispatcher, `30..41` for the checker
and the branches, `42..44` for the rebase. Each prefix is the ordinal of the
corresponding JSON file in the session.

Evidence examples: dispatcher
`ev_74fec9dcbae64fe9e3fd8c5dd4163880c2dddda8e82b6bb27cc1c3c6a020e4b2`,
advance `ev_1b3d602a8e99f46d46147131976e3dc641c270bd6710a2f339ad39a30f491cc4`,
rebase `ev_cc68039ea2be484a2083f9ce6df911268b996208fa56d934bb1dc6b25c501c93`.

The [bounded, owning C++ reader](vmf_movie_reader.md) is now integrated.
Script execution and the context services remain to be reconstructed.
Trial 25 constitutes the new, strictly bounded official evidence indicated
above; no static presence is treated as an observed call.
