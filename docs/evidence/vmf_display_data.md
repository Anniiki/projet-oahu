# VMF interface placements and removals

## C++ result and scope

`tdur_vmf_display_data` decodes tags 26/28 of a validated `VmfMovie`.
`VmfDisplayData` owns the fields, names and Xbox prefixes, without keeping a
view onto the film. Events are indexed by the player's global frame; each
slice keeps the original order of placements and removals.
Empty frames also have a slice. This prepares the construction of the
instances; this module is neither a live display list nor a renderer.

The structural reader stays unchanged and keeps these payloads opaque. This
separate library adds their typed interpretation and its guards.

## Established PC grammar

At the start of the tag: `depth:u16@8`, `flags:u16@0A`, cursor 12. The
following fields are present depending on the flags:

1. `2`: characterID **WORD**, with no immediate alignment.
2. `4`: align to 4, then float32 pairs in the order `100`, `80`, `200`.
   Each bit adds 8 bytes; the absent components are `(1,1,0,0,0,0)`.
3. `8`: align to 4, then eight raw WORDs. Their signedness is not imposed.
4. `10`: ratio WORD; `40`: clip WORD; `20`: NUL-terminated name.
5. Align to 4 up to the exact end of the record, with zero padding.

The clip precedes the name, even though the corpus never combines these two
bits. References and ratios are not DWORDs. Thus flags 13 decodes into two
WORDs at offsets 0C/0E, in a 16-byte record; no special exception.
Names keep all their bytes, without mandatory UTF-8 or truncation to 31.
The 31-byte copy of the future instance node is a separate operation.

## Xbox donor and removals

Xbox has a fixed prefix: depth/character at offsets 8/A, six big-endian
floats at C, eight WORDs at 24, flags at 34, ratio at 36, a reserved WORD at
38 and a candidate clip at 3A. The name starts at 3C. The **52 physical bytes
from 8 to 3B** are preserved, including the inactive fields. The active
optionals follow the flags.
Both Xbox occurrences of bit 80 are preserved.

This layout comes from the resources and from comparisons between editions,
without XEX decompilation or proof of equivalence with the native Xbox player.
The 19 matrix differences in DEMO_END are real differences between editions;
the decoder preserves them.

Removals are 12 bytes: depth WORD at 8, raw WORD at A. Reading a big-endian
DWORD would wrongly multiply non-zero depths by 65536.

## Native sources and limits

Secondary PC SHA-256:
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
Rea session `reprise-2026-10-31/rea/vmf-host-context/session-v2/`:
resolutions 9..14 and assemblies 18..23, functions 446B70, 44AD20, 44ADB0,
44AE10, 44AE80, 44AF00. The accessors establish widths, alignments,
defaults and the order of the PC fields. The initialiser 4487F0 links
`resource+E` to the list capacity used by 446B70. The
[instance contracts](vmf_instance_contracts.md) detail this relationship
and their limits.

Three small virtual wrappers have no function recognised by Ghidra.
Pseudocode 17..19 from the first session used the documented file-offset
fallback towards another function: they are explicitly discarded.
Their instructions were then verified with bounded DUMPBIN and exact Rea
reads. Four windows, **2179 bytes**, match; private report
`exact-byte-validation.json`, SHA-256
`c444057f7775fda5c82582a53b5089eb2d67233b03a1c9c35fd221ff1867d258`.
No promotion to official code and no new native invocation is made.

## Guards and validation

All reads stay within the record. Present active IDs, depth below the
declared capacity, finite active matrices, observed flags, NUL and padding
are verified. Inactive Xbox fields stay raw.
The input, collection, name and logical output quotas are checked before
any allocation. Copying is independent; assignment prepares a complete copy
before replacement. Access to events by frame is constant-time.

The harness and the 39 oracles are frozen before the CPP. The first synthetic
envelope omitted the root capacity from the header; it failed in `VmfMovie`
before any call to the new decoder. Only this envelope and its diagnostic
were corrected: 73/73 valid envelopes on both architectures, without
modifying the field or quota expectations. The first freeze is preserved.

**x64 and Win32** results: 528 synthetic checks, eight injected decoding
allocation failures and eight injected copy allocation failures, then
**19031828 checks** with the corpus.
The 39 files cover 277151 placements, 27866 removals, 132788 frames and 305017
events. All comparisons pass, without modifying the references.
This validates data, not animation, hit-testing, masks or co-op.

CTest: `vmf_display_data`. x64 integration: **59/59 tests passed**.
