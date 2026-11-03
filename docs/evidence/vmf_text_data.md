# Type 37 VMF text data

## Evidence and scope

The PC Paradise secondary executable carries the SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
The private exports `reprise-2026-10-31/rea/vmf-text-bindings-v2/session/`
establish the following path:

- `21`, 4418B0: the loader replaces the first DWORD of the record with a
  vtable, at the same location. The type table `23` gives `table[37]=13`; the
  jump table `22` gives `jump[13]=441CA8`, which writes `F3ECF8` to `[ESI]`.
- `17`: the virtual slot +10 of that table points to 4416C0. The reads
  `14/31` fix its getter as six bytes returning 37. Ghidra recognizes no
  procedure at that address; no invented resolution is used.
- `26`, 44B030, called through slot +8: writes byte 2F according to
  `context+2C` and `flags2E&8`, then resets caches 70/74 to zero. This body
  does not insert a definition. Byte 2F must therefore not be called padding.
- `11`, 441700: builds the context and calls 44A350 at 4417D0.

`exact-byte-validation.json` compares three recognized bodies and the bounded
getter against the PE and DUMPBIN: **1,477 bytes**, matching instruction
starts, unchanged source. Snapshot SHA-256
`6f76c9d820da18c1b48a5c78234fef19916f41038f3035cdf02e8238a3f75880`.
This remains secondary static evidence, with no new official invocation.

The 39 verified private VMF files contain 4,644 PC fields and 4,871 Xbox
fields. The Xbox profiles come from observed data; the PC ASM does not prove
their loader.

## Owned C++ model

`VmfTextData::decode(movie, limits)` keeps IDs/indices, the name terminated
within 60 bytes, the owned initial text and the raw fields 2E/2F. WORD 30
exists only on PC. String bytes stay in their source encoding; no Unicode
conversion, input limit or color/unit meaning is assumed.

| Profile | Name | Disk caches | Initial text |
| --- | --- | --- | --- |
| PC 2.8 LE | 32..6D | 70/74 | 78 |
| Xbox 2.5 BE | 30..6B | 6C/70 | 74 |

The name requires its first NUL within 60 bytes; the unused tail is ignored.
The text requires a NUL followed by at most 3 alignment zero bytes. Non-zero
disk caches are refused by this narrow reader, never dereferenced. This modern
choice is stricter than the native initializer, which resets them to zero.
The WORD FontID selects a type 10 record; its DWORD +C provides a companion
reference. The 9,515 references of the corpus resolve to a type 20 record,
without that relation being enough to reconstruct glyph placement or drawing.

The VMF reader enforces dense definitions: its static indices are not an
availability indicator in the future per-frame runtime table.
Quotas: number of fields, text size including NUL, logical storage
`sizeof(VmfTextData)+N*sizeof(field)+Σ(text.size()+1)`. Capacities/allocators
excluded. Errors/OOM publish no partial decode result. Views must not outlive
the owner or survive its move/assignment.

## Wiring and validation

`VmfProgram::build(movie, display_limits, budget, VmfTextLimits{})` enables
the option and counts its data in the global budget. The existing API
without a fourth argument keeps the opaque mode for type 37 resources.

```powershell
.\build\Release\tdur_inspect_vmf_playback.exe movie.vmf pc --variables --text-data
```

The inspector adds `text_fields`, `text_font_links`, `text_companion_links`.
The two flags are independent; duplicates/unknown options are refused.
The corpus is checked with/without each option on x64/Win32, then compared to
the earlier diagnostic. Synthetic tests cover profiles, 59-byte names, NUL,
padding, caches, references, ID order, ownership, budgets and every OOM.
The first invalid fixtures (header and non-dense definitions) were corrected
without changing the parser guards.

The [44BDC0 initialization](vmf_text_bindings.md) is now wired through an
explicit backend to the variables, the transactional cleanup and the runtime
caches of PC texts with bit 2E&10. Decoding alone keeps type 37 placement
opaque. Registry/focus, real input, layout and glyph rendering remain. No
stop on opcode 83 is acknowledged. No commercial menu, chat or co-op is
validated by this support.
