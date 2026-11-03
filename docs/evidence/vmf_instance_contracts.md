# VMF instances, frames and variable scope

## Evidence status

Contracts prepared for the future C++ host, established on the **secondary**
SHA-256 `f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
This static evidence does not establish an invocation in the official game.
The [C++ runtime](vmf_display_runtime.md) now carries the instance lifetimes;
the complete wiring to the scripts and to the rendering remains to be done.
The exports come from the private batch
`reprise-2026-10-31/rea/vmf-host-context/`; the resource oracles provide
values, without establishing a live instance or its scheduling.

## Construction and capacity

Vtable F3F7BC: first pointer 448790. This wrapper has no function recognized
by Ghidra; its 96 bytes, read with Rea and DUMPBIN, agree. It attaches the
parent and the resource, then calls 4487F0, recognized with 282 contiguous
bytes (resolution 25, assembly 27). The initializer establishes:

- `current_frame +60 = 1`, `requested_frame +70 = 1`, flag `+68 = 0`;
- playback state `+64 = 2` if the resource holds a single frame, otherwise 1;
- list capacity `+24 = WORD[resource+E]`, head `+1C = 0`;
- initial transform properties, raw colors and counters reset to their own
  values. Their rendering effects are not ported yet.

The placement handler 44AF80 passes `instance+1C` to 446B70. The latter
compares the record's depth WORD with `[list+8]`, hence with `instance+24`.
This ties the depth guard to the resource's `raw_0e`, beyond the mere
correlation observed in the corpus. The display-list node and the child
instance are distinct objects.

## Playback and frame requests

Assemblies 5/6 of the first session: Play 444360 writes 1 to `+64`, Stop
4443A0 writes 2. The current frame, requested frame and transition fields stay
separate. GotoFrame 444110 adds 1 to the u16 argument, clamps to FrameCount,
writes the request and its difference flag, then Stop. Next 4442C0/Previous
444310 compute from the **current** frame, without themselves changing the
Play/Stop state.

The `VmfMovie` data uses 0-based frame indices; the runtime fields above are
1-based. 448E60 applies steps and processes the action/child queue; its
complete resumption and scheduling contract remains to be ported. An immediate
assignment of the current frame would be premature.

## Scope identity

4487F0 reads the DWORD `context+B0`, stores it in `instance+E0`, then
increments it. Assembly 32 of 4418B0 confirms `XOR EDI,EDI` then
`MOV [EBP+B0],EDI` on the fresh-initialization path, before the root creation.
The scope is therefore a **numbered ID32**, with root 0 valid, distinct from
the instance pointer and from the resource characterID. The native INC has
no guard against wrap-around.

Get 449070 (assembly 17 v2) and the setters reuse this DWORD as the dictionary
seed. A path without a variable name can produce a sprite reference; an empty
string after `:`, by contrast, remains a genuine empty key. The future x64 port
must keep these identities separate from its handles and generations.

The private review `vmf-host-variables-review/` covers 29 exports, 8490 hash
vectors and 28 synthetic scope allocations. The hash keeps the high bytes of
the last partial word; keys are stored on 31 bytes, but the hash and the
lookups receive the full name. The CRT comparison depends on LC_CTYPE. The new
analysis of 4438D0 refines deletion: a non-zero argument selects its own scope,
but **zero selects every non-zero scope**, keeping the scope 0 entries. 448910
calls it only if bit 200 of the instance is set, before destroying its display
list. See [the lifecycle](vmf_display_lifecycle.md) for the evidence and the
cleanup order. The locale actually in effect, the creation and lifetime of
external texts 2/5 and the complete path grammar remain open. These findings
are not a C++ implementation of the table nor of the script host.
