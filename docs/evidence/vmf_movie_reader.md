# Owning VMF reader: contract of the observed subset

## Status and sources

The interface was frozen before implementation and before the independent
tests: [`vmf_movie.hpp`](../../include/tdur/assets/vmf_movie.hpp).
This phase decodes a structure and bytecode; it executes no instruction,
host command, game logic or rendering operation.

The grammar and the 39 resource identities are established in
[vmf_format_inventory.md](vmf_format_inventory.md), with the independent
numerical review of the private batch `vmf-format-review/`. The batch of this
delivery is `<private-evidence>/reprise-2026-10-30/vmf-movie-cpp/`.
No commercial resource is embedded in the sources or fixtures.

Secondary Rea anchors, Paradise PE SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`,
batch `rea/ui-map-input-dispatch/session/`:

- `442890/161`, exports `34-procedure_assembly` and
  `35-procedure_pseudo_code`, validates magic/CRC/version; the library
  presents itself as 2.8 and requires byte `+05==2`, u16 `+06==8` on PC. Evidence
  `ev_6e5a14a0d8bcc5d832317e8a44bb73c649563bf4c07f72db4f36ce77948bc69b`.
- `443A90/83`, exports `40-procedure_assembly` and
  `41-procedure_pseudo_code`, advances by two physical bytes and one logical
  byte for a short instruction; by `4+align2(len)` physical and `3+len`
  logical bytes for a long one. Evidence
  `ev_3b5cb3876fab40278923a7089e601169e4cab03faf331d344894f8d3d7d72e75`.
- `443EF0/157`, exports `42-resolve_containing_procedure`,
  `43-procedure_assembly` and `44-procedure_pseudo_code`, returns to the start
  of the script and rescans while counting physical and compact bytes separately.
  `445560` advances first, then applies the signed16 delta to the compact counter;
  `4455B0` does so if the popped numeric condition is non-zero. Evidence
  `ev_cc68039ea2be484a2083f9ce6df911268b996208fa56d934bb1dc6b25c501c93`.

These static exports come from the secondary. [Official trial 25](official_trial_25.md)
then corroborates the bodies of the checker, the advance and the rebase in
the official executable in memory; the CRC helper remains outside the capture. The port
does not copy the pseudocode and recreates neither native relocation nor interpreter state.

## Profiles and header

`VmfMovie::parse(bytes, profile, limits)` requires an explicit profile:
`pc_2_8` (LE) or `xbox_2_5` (BE). No retry with the other profile after a failure.
An unknown enum value is refused. The Xbox 2.5 version is not presented
as accepted by the native PC 2.8 library.

The reader checks the 72 header bytes, `VMF\0`, major 2, the minor specific to the
profile, and the byte `+08` matching the profile (2 PC / 1 Xbox).
`raw_04`, the second byte of `raw_08`, `raw_0a`, the 32 author bytes,
`raw_3c`, `raw_40` and `raw_42` are preserved without inventing a meaning for them.
The author is not required to be a NUL-terminated string or to equal Eden Games.
The rectangle is kept as four finite float32 values; its units are not
converted. Raw values are not used as addresses.

CRC32: the first 68 bytes are normalized to LE as in the inventory,
then compared with the native DWORD `+44`. No byte of the supplied buffer is modified.
`raw_40` must equal `raw_0e` of the root sprite, a corpus relation retained
as a restriction of the port.

## Container and references

The reader accepts the **17** observed types:
`0,1,2,7,9,10,11,12,14,15,20,26,28,37,39,43,46`.
Any other type is `unsupported_record`. The payloads of these types that are not fully
decoded are opaque; this does not validate their geometry, text, sound or undecoded
internal references.

Each read is prevalidated within its own region. Records start
on a four-byte boundary. The length includes the header and is a
multiple of four; type 39 distinguishes metadata from the full extent.
The reader bounds a child within its parent **before** examining its body.
The root ID 0 starts at 72; its End records are type 0 / size 0 over eight bytes.
Each child has type 0 / size 8. No record follows the End of a sprite.

The covered sprites have at least one frame. The metadata equals
`24+4*FrameCount` and its repetition at `+14` must agree. Each frame end
offset is relative to the tag, increases strictly and coincides with a direct
ShowFrame plus eight. The last one is `extent-8`. The frames of children are
not added to those of their parent.

Definitions are dense, unique, `0..definition_count-1`, with ID 0 for
the root. They are exposed by increasing ID; records keep the physical
order. The ID of type 10 occupies two bytes, the others four. The reader
checks every reference **that it decodes**: the character_id of a button
visual and the ID of a TXIX entry must point to present definitions.
It does not claim to have checked the internal references of opaque payloads.
`definition(id)` directly returns the index into `records()`, that is
`definitions()[id].record_index` for a valid ID, not the index of the entry
in the dense table.

## Buttons, scripts and targets

The covered type 7 has a single 48-byte visual. Its offset is read within the region
of the tag, then its record and its terminator are bounded before reading. The
placement formula makes it possible to detect a variant with several visuals,
but **N>1 is explicitly unsupported**, never extrapolated. The extra PC
DWORD, the states, the 16-bit word, six matrix floats and eight 16-bit words
are preserved. The floats must be finite; no physical input state
or rendering matrix order is attributed to the other values.

Each condition is `{size16,flags16}` followed by a stream: a non-zero size
leads to the next **physical** header, aligned on four bytes; zero designates
the last region. Flags remain raw. Type 12 has a timeline stream
associated with the sprite/frame; type 43 keeps its string and its owner.

The action subset is exactly:
`00,04,05,06,07,0A,0B,0C,0D,0E,0F,10,11,12,13,14,15,17,18,1C,1D,20,21,22,23,26,81,83,8B,8C,96,99,9A,9D,9E,9F`.
Any other value is `unsupported_action`. Short header u16; long header
u16+len16, bounded payload, zero padding to two bytes. End is an exposed
instruction; after it, at most three zero bytes are admitted in the region.

The covered long forms: `81` two bytes, `83` two NUL-terminated strings,
`8B/8C` one NUL-terminated string, `96` a type byte 0 followed by a NUL-terminated string,
`99/9D` a signed16 delta, `9A/9F` one byte, `9E` zero bytes.
Strings occupy exactly their payload, without assuming
UTF-8; the exposed ranges exclude their NUL. The alternative forms of Push
or GotoFrame2 remain `unsupported_layout`, not an invented interpretation.

A branch is translated from the **compact** end of the instruction to an
instruction of the same stream. It may target End, not the byte after End or
another stream. `target_action_index` is a global index into `actions()`;
no host address or pointer32 value is exposed as executable.
This validation is deliberately stricter than the native secondary goto:
its rescan may stop when the compact counter reaches **or exceeds**
the target. The reader requires equality on a boundary and refuses the
malformed script rather than reproducing an overshoot.

## Images and TXIX

The trailer starts immediately after the root extent: native u32 magic
`0x54584958`, u32 count, then 32-byte records. Its end must be the end of the file.
The observed PC profile has an empty trailer; a non-empty PC TXIX is outside this
subset. Xbox may have a zero count.

Each entry preserves **ID + page + two raw DWORDs + four floats**, that is
32 source bytes. The ID is unique and points to a type 20; the floats are finite and
within `[0,1]`, without naming their UV orientation. The page number is a
value, not an arbitrary limit at the maximum of 8 encountered in the corpus.

A type 20 image without TXIX yields `direct_fla` and its ID; with TXIX it yields
`atlas_page`, the page number and the entry index. The class resolves no
name or BNK member, loads no 2DB and does not check its availability.
The inventory conventions `fla_%04u.2db` / `page_%03u.2db` remain an explicit
task of the future resource resolver.

## Ownership, views and quotas

The buffer is copied and all collections own their values.
The structures store no span or string_view of the file; their links
are range32 values and indices. A copy of the movie is independent. The returned
views belong to the consulted object and must not outlive its
destruction or a move/assignment involving it. All view getters
are `const&`, and their `const&&` overloads are deleted. The input must
remain stable during parse; once it has returned, the input may disappear.
Copy assignment prepares a complete copy before a replacement by
`noexcept` move. A failed allocation therefore keeps the destination movie
fully intact. Self-assignment makes no copy.

`bytes(range)` accepts the empty range exactly at the end of the buffer; it refuses
an offset/size exceeding the source, without an overflowing addition. Internal
ranges are prevalidated. Operations other than destruction/assignment
on a moved-from object remain outside the contract. A moved-from object may receive
a new value; it must not serve as a copy source.

The limits and their defaults are public in the header. Each counter,
addition and product is checked before growth/allocation; root
depth 1. A zero budget is effective. The logical budget is precisely:

```text
source_bytes.size + sizeof(VmfHeader)
+ records.size * sizeof(VmfRecord)
+ sprites.size * sizeof(VmfSprite)
+ frames.size * sizeof(VmfFrame)
+ definitions.size * sizeof(VmfDefinition)
+ scripts.size * sizeof(VmfScript)
+ actions.size * sizeof(VmfAction)
+ button_visuals.size * sizeof(VmfButtonVisual)
+ labels.size * sizeof(VmfLabel)
+ images.size * sizeof(VmfImageReference)
+ atlas_entries.size * sizeof(VmfAtlasEntry)
```

Vector capacities, allocators, container objects and temporary indexes are
outside this count; the other quotas bound their numbers. The budget varies
with `sizeof` between x64 and Win32. The source must also fit within offset32 values.
A failure returns an error/code/offset and no partial movie. Diagnostics
have static lifetime. Several simultaneous defects may determine
a different first error; tests must not impose a priority order
between independent defects.

## Prevalidation in the implementation

The traversal uses an explicit work stack, bounded by `max_depth`,
without C++ recursion over an external file. Persistent links are
indices; no local reference is reused after its collection has grown.
The instructions of a stream are all added before its
branch resolution.

| Region | Guard before reading or visiting |
| --- | --- |
| Integer/float field | `u16/u32` call `require` within the supplied bound before any source index |
| Sprite child | metadata, extent and frame boundary are bounded within the parent before `push_sprite` |
| Frame table | expected size and all offsets are checked before frame allocation |
| Long action | four header bytes, then payload and padding are bounded within the stream before reading |
| Button | off field, terminator and visual within the tag; each header and condition region within that same tag |
| String | NUL search limited to its payload, without a C call on an unbounded string |
| TXIX | eight fixed bytes and the count×32 product checked before arrays; each entry read within its 32 bytes |

This inspection of the paths complements the refusal tests. An observed refusal
alone does not demonstrate that an implementation did not read a neighboring region
before refusing.

## Validation performed

Independent source-owned tests in
[`vmf_movie_test.cpp`](../../tests/vmf_movie_test.cpp). The oracles were frozen
at **21:05:39 UTC on 2026-10-30**, with a recorded absence of the CPP in
`vmf-movie-tests/oracle-frozen.json`. Their author did not read the implementation
to write or adapt them. The expectations remained unchanged.
An eleventh scenario, an allocation interrupting assignment, was then frozen
before its fix; it reproduced two inconsistencies on both architectures.

MSVC **19.44.35229**, `/std:c++latest /W4 /WX /O2 /EHsc /MD /permissive-`,
private compilation and link successful on x64 **and** Win32:

| Run on each architecture | Checks | Failures |
| --- | ---: | ---: |
| Final synthetic fixtures | 1 346 | 0 |
| Same executables, fixtures +39 copies, without rebuild | 120 468 | 0 |

The assignment case injects a metadata fault after the copy of the
temporary source: bytes, collections, IDs, ranges and counters of the destination
movie remain unchanged. The copy succeeds after disarming;
self-assignment and a moved-from destination are also covered. The number of
injection checks follows the allocations actually traversed.

The fixtures discriminate profiles/CRC, defs out of physical order/font16 ID,
parent vs file, stream/button-specific sizes, two target
spaces, End/returns/negatives/interiors, 36 opcodes, opaque payloads, raw
flags, TXIX, each quota and its boundary, copies/move/rvalues and ranges.
Each ordinary allocation observed on the parse path of a fixture is
made to fail in turn; the reader returns `resource_limit` with no
escaped exception. This counter does not intercept every allocator or
direct malloc calls.

The read-only traversal of the 39 copies finds **503 752 records, 5 349 sprites,
132 788 frames, 27 685 scripts, 520 158 actions and 31 816 branches**. The 39 SHA
values before/after match the manifest and are unchanged. The original banks
are not reopened in this phase; their audit belongs to the inventory.

The first batch, before this regression, passed 1 325/120 447 checks; its
historical evidence remains in `vmf-movie-tests/validation.json`, SHA-256
`809315c341b4675338cc8d67885bdb646db665672d8bd1be724d7b8b2f8abd6f`:
PE machines `0x8664/0x14C`, fingerprints of the executables/logs and sources, results
and limits. The red-then-green evidence of the fix is
`vmf-movie-tests/copy-regression-validation.json`, SHA-256
`5b1dbd1d5f7911db5bcc1b8124f216e128fe6ea3b03faa0fe4b5df5f9272b517`.
The targeted CMake integration also passes the fixtures and the 39 movies on x64
and Win32: `vmf-movie-parent-integration-v1/`.
The private bench can be rebuilt from the retained evidence; the executable
accepts `--corpus <selected-vmf-v1>`.

Final sources tested, after the assignment fix:

- Header: `c81a1dfd49d9ba000eb3e53d5de8825d063b9c875238622a1368121f97c6963d`.
- CPP: `3a214d98a8892c52fdde10528ca343d164d0d20d4cea9946f2d1b8232828a0bc`.
- Test: `dc33cf0d9b6c1fb88735d28f355954a846eadf04aeda09f8aeb7a6d54bbc6855`.

These results validate the bounded reader of the subset and its owning
outputs, not an interpreter, a renderer, the controller mapping, playable menus
or the entirety of the VMF variants. No game, console, executed VMF or native
call is involved.
