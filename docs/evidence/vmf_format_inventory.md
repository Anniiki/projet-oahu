# VMF: minimal grammar of the Xbox 360 and PC interfaces

## Scope and provenance

Static study of 30 October 2026. It complements
the donor inventory: **20 Xbox VMF and
19 PC VMF**, i.e. **23,844,792 bytes**. The Xbox files come from the banks
already extracted in the `xbox360-controller-inventory` batch; the PC files are
the members of the inventoried local official installation, not a new
extraction from the DVD. No VMF, game, XEX or script is executed.

Private batch: `<private-evidence>/reprise-2026-10-30/vmf-format-inventory/`.
`selected-vmf-manifest-v1.json` keeps bank, physical member, path, offset,
size and SHA-256; the banks are verified before/after read-only copying. Its
source index `ui-pairs.json` has the SHA-256
`aa269f791a6bc8248c1de8de0682f5d8dd4545ba5665ade1df76547ef52b109d`.
Only the private copies `selected-vmf-v1/` feed the parsers.

`map.vmf` witnesses, physical member 0:

| Platform | Offset in its bank | Bytes | SHA-256 of the member |
| --- | ---: | ---: | --- |
| Xbox | `0x1000` | 825,516 | `8d11a53e7663d8b047e6a2980886c197f01199e01aa4dba5deca161e2d2608ec` |
| PC | `0x19C0` | 780,940 | `42c80a821dd132da105c20927ca00a1838dc73fb9c6d46da5200c21ab59ce140` |

These results concern this corpus. They constitute neither a complete decoder
for all VMF versions, nor proof of rendering or of playable behaviour.

## 72-byte header

The scalars described below are **BE on Xbox, LE on PC**; strings remain
bytes. Unknown fields keep their raw position.

| Offset | Verified representation | Observation in the 39 films |
| --- | --- | --- |
| `+00` | 4 bytes | `VMF\0` |
| `+04` | 2 constant bytes | `00 02`, do not systematically read them in native endianness |
| `+06` | `u16` | 5 Xbox, 8 PC; meaning not established |
| `+08` | 2 bytes | `01 02` Xbox, `02 02` PC; first byte compatible with an endianness marker, not yet attributed to the loader |
| `+0A` | `u16` | 12 Xbox, 14 PC; meaning not established |
| `+0C` | 32-byte field | `Eden Games\0`, zero-filled remainder |
| `+2C` | 4 × `float32` | rectangle; map: `0,25600,0,19200`, units not proven |
| `+3C` | `u16` | 30 or 60, plausible nominal film rate, not a measured rate |
| `+3E` | `u16` | number of definitions, **not** number of frames |
| `+40` | `u16` | value equal to field `+0E` of the root sprite; role unknown |
| `+42` | `u16` | role unknown; do not call it the number of sprites |
| `+44` | `u32` | CRC32 of the normalised header, described below |

The DWORD `+08` is not the file size: its bytes are constant per platform
despite the different sizes. The contrary comments of the old local
converter are not taken as evidence.

CRC: compute the standard CRC32 of the **first 68 bytes in canonical LE
representation**. On PC, the buffer is already canonical. On Xbox, swap the
u16 at `+06,+0A`, the four 32-bit words at `+2C,+30,+34,+38`, then the four u16
at `+3C,+3E,+40,+42`; keep the other bytes. The result equals the native u32
at `+44` on **39/39**. The CRC of the raw Xbox BE buffer does not match.
This check does not prove when the native software performs this conversion.

## Records, definitions and sprites

From `0x48`, a record starts with `{type:u32, size:u32}`. The observed sizes
are multiples of four and include this eight-byte header.
The types encountered are `0,1,2,7,9,10,11,12,14,15,20,26,28,37,39,43,46`.
The comparison with Flash tags is useful, but their payloads are
transformed: a standard SWF tag parser cannot read them directly.

Types `2,7,10,11,14,20,37,39,46` define identities. Their set is exactly
`0..header[+3E]-1` in each film, without duplicates; their physical order can
differ from that of the IDs. The ID is u32 at `tag+08`, except for type 10
where it is u16. The type 39 root carries ID 0. An ID table must be built
without assuming their physical order.

For **type 39**, the record size is only that of the metadata:

| Offset relative to the tag | Field |
| --- | --- |
| `+08` | ID u32 |
| `+0C` | `FrameCount` u16 |
| `+0E` | raw u16, meaning not established |
| `+10` | u32 extent of the complete sprite **from the base of the tag**, End included |
| `+14` | u32 repeating the metadata size |
| `+18` | `FrameCount` u32 offsets of frame ends |

Across the **5,349 sprites**, `metadata_size = 24 + 4*FrameCount`. The children
start at `tag+metadata_size`. For each entry `i`,
`tag+frame_end[i] == ShowFrame[i].offset+8`; do not interpret these offsets
as absolute. The last one equals `extent-8`. The number of **direct** ShowFrame
equals FrameCount: the frames of child sprites have their own schedule.
The terminal End takes eight bytes: type 0/size 8 in nested sprites,
type 0/size 0 for the 39 roots. A hierarchical walk skips a type 39 child by
its complete extent, not by its metadata size alone.

## Type 7 buttons: layout actually covered

The **3,019 type 7 tags** each contain **a single** 48-byte visual record.
The corpus does not demonstrate the variant with several records.

| Field | Xbox | PC |
| --- | --- | --- |
| ID u32 | `+08` | `+08` |
| Additional DWORD | absent | `+0C`, value 0 or 1, meaning not attributed |
| `off` field u32 | `+0C`, equals 64 | `+10`, equals 68 |
| start of the visual record | `+10` | `+14` |

This record contains `states:u32` (bits 0..3 only), `character_id:u16`,
a raw u16, six finite float32 then eight raw u16. All character_id values
refer to a present definition. The order of the matrix and the meaning of the
eight u16 are not yet a rendering contract.

A zero DWORD is present at `tag+off`. The first condition header starts at
`tag+off+4`: `{size:u16, flags:u16}`, then the actions. `size>0` leads to the
next header from its own base, in **physical VMF bytes**;
`size==0` ends the chain. Each region ends with End then at most three zero
bytes. The flags stay raw: VMF type 7 contains chains of conditions, like SWF
DefineButton2, without reusing its tag number 34.

## Actions and two offset spaces

The **18,822 type 12 tags** and **8,863 button conditions** decode entirely
according to this observed grammar, without arbitrarily scanning the file:

```text
opcode u16 native
if opcode >= 0x80 : payload_size u16, payload[size], zero padding to 2
otherwise         : no payload
opcode 0          : End ; rest of the region zero, at most 3 bytes
```

Result: **520,158 instructions**, **36 distinct opcodes**. The 168,007 Push
`0x96` each contain a type byte 0 followed by a single NUL-terminated string;
three strings have non-ASCII bytes. Preserve the bytes, without imposing UTF-8.
Nested functions/With/Try are not encountered and remain outside the evidence.

The Jump `0x99` / If `0x9D` branches have a signed 16-bit delta. To resolve
them, build a table in the **recompacted SWF** space: short opcode 1 byte,
long opcode `3+payload_size` bytes, without the VMF padding. Add the delta to
the logical end of the instruction, then translate the target to the VMF
offset. The **31,816** branches thus land on an instruction: 6,141 on End,
25,675 on another instruction, none after End. Applying
`physical_next+delta` would give 26,660 targets off a boundary **and 4,431 on a
wrong boundary**; only 725 would be correct. The historical field
`physical_valid` of the reports only designates an alignment, not a correct
target. Conversely, the sizes of type 7 condition chains are indeed
physical. These two rules are not interchangeable.

The opcode names and the relative ordinal counter model come from the
[Adobe SWF19 specification, mirror copy of the primary document](https://open-flash.github.io/mirrors/swf-spec-19.pdf),
Actions and buttons sections. It also describes keyboard conditions,
SetTarget and DefineSprite. It serves as a comparative reference: the widths,
alignments, frame tables and VMF tag differences above are established
separately on the resources, not automatically inferred from SWF.

## Executable relationships to preserve in the future client

The 10,429 GetURL `0x83` each carry two strings: the first starts with
`FSCommand:`, the second is empty. These are instruction sites with an
owner, frame or condition, not merely textual occurrences.
The 1,538 dynamic GetURL2 `0x9A` are not included in this count of static
commands; no path is declared executed or reachable here.

Map witnesses:

- Xbox, root timeline frame 9 counting from zero, tag 12 `0x810DC`: SetTarget
  `curseur`, Push `/:WhatZoom`, GetVariable, GotoFrame2, return to empty target.
  The same sequence on PC is found in tag `0x8848C`.
- Xbox button ID 997, tag `0xC5FD4`, conditions 1/2: raw flags
  `0x7200/0x6E00`, commands `zoin/zoou`. The first compares
  `1 < /:WhatZoom`; the second compares `/:WhatZoom < "5"`. A branch
  avoids the out-of-bound command. The script saves `zoom_tmp`, sends the
  command, **reads back** WhatZoom for GotoFrame2, then decides the sound
  according to the change. This is not a simple button whose text changes.
- PC ID 1060, tag `0xBD5FC`, conditions 1/2: flags `0x5600/0x5A00` and upper
  bound **"4"**, not "5". The 21 PC button scripts reading
  `/:WhatMouseButton` have no equivalent read in the Xbox map.

The candidate SWF reading of the keyboard bits gives `flags>>9 & 0x7F`: codes
57/55 Xbox, 43/45 PC. How physical buttons produce these codes remains
unknown; no controller assignment is invented.

On the secondary native side, the coordinated batch
`rea/ui-map-input-dispatch/session/` is based on the Paradise PE SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`:

- `443AF0/760` reads 16-bit opcodes, separates values below `0x80` and above,
  then dispatches notably `83→445CB0`, `96→444510`, `99→445560`,
  `9D→4455B0`. The post-processing calls `443A90` if the state DWORD
  `+10==1`, then resets that DWORD to 1. Sources: `25-resolve_containing_procedure`
  and `29-procedure_pseudo_code`, evidence
  `ev_74fec9dcbae64fe9e3fd8c5dd4163880c2dddda8e82b6bb27cc1c3c6a020e4b2`.
- `445CB0/144` validates `FSCommand:` over ten bytes; the suffix is passed
  to the callback `[global10E8078+9C8]` with userdata `[+9CC]`. Sources:
  `12-procedure_assembly` and `16-procedure_pseudo_code`, evidence
  `ev_4aba5265111b804d5e5c075189c193b30575b341e8cd8b08109207b167835894`.
- `8DC790/84` writes `/:WhatZoom` through `440BA0`: the states `[this+240]`
  `1,2,3,4,5` give `5,4,3,2,1` respectively. Sources:
  `11-procedure_assembly`, `15-procedure_pseudo_code`, table `17-read_bytes`;
  evidence `ev_0272aba170aef62307290b454e95674ef31e694a7d65fe8d11443bb7e83d7f35`.

The dispatcher agrees with the 16-bit opcode grammar; this does not yet
demonstrate its internal strategy for translating the deltas, nor the whole
loading path.
These bodies remain **secondary**, without new official corroboration or
observed execution. They demonstrate no reproduced Xbox ergonomics.

## Images and TXIX trailer

After the extent of the root comes the native u32 `0x54584958` (bytes `TXIX`
on Xbox, `XIXT` on PC), then a u32 count and that many 32-byte records.
Their end exactly equals that of the file: zero records on the 19 PC files,
**1,809** in total on Xbox. Each record contains a u32 ID, three DWORDs,
then four finite float32 in `[0,1]`; each ID is a unique type 20.

The **3,853 type 20 image definitions** break down with nothing missing:

- **1,809 TXIX entries**: the second DWORD of the entry matches the numeric
  suffix of a `page_%03u.2db` file present in the same bank, for every
  entry. The next two DWORDs and the four floats are compatible with
  dimensions and an atlas rectangle; orientation and sampling remain
  to be typed.
- **2,044 other images**: the file `fla_%04u.2db`, with the exact VMF ID,
  exists in the same bank for each of them. This number is not the physical
  index of a BNK member. No ID offset is added.

These links are verified against the already inventoried names of the banks,
not by rendering the textures. The type 20 records of 40/44 bytes (payload of
32/36), the Xbox 2DB formats, the fonts and their atlases are not fully
decoded here.

## Coverage, limits and next delivery

| Check | Xbox | PC |
| --- | ---: | ---: |
| Films | 20 | 19 |
| Structural records | 255,052 | 248,700 |
| Sprites / cumulative direct ShowFrame | 2,681 / 67,726 | 2,668 / 65,062 |
| Action streams | 12,543 | 15,142 |
| Instructions | 193,228 | 326,930 |

The private parsers impose limits on file, depth and records, and verify
sizes/alignments/extents, frame tables, IDs, links and branch targets. They
are corpus verifiers, not a library ready to accept hostile files. They use
no OpenTDU engine code.

The independent numeric review recomputes the counts and targets from the
39 files, without importing these parsers or their JSON trees:
`<private-evidence>/reprise-2026-10-30/vmf-format-review/verify_numeric.py`
and `numeric-review.json`. It confirms the identities, the CRCs and the values
above. It highlights prevalidations to add **before reading** in the future
C++: bound a child's extent against its parent sprite before recursion; bound
the four bytes of the long action header against its own stream; bound the
button terminator against its own tag. The exploration scripts do not impose
these three guards at the same stage and must not be transposed as-is into a
parser of untrusted data.

Realistic next module: owning C++ reader of the container, ID dictionary,
timelines, raw conditions and normalised instructions with target
conversion. Preserve both button variants and the unknown bytes; refuse an
uncovered variant rather than continuing after misalignment.
The execution engine must then type the stack, the variables/target paths,
the string/number/boolean conversion and the reentrancy of host commands,
particularly the immediate read-back of WhatZoom.

The [placements 26 and removals 28](vmf_display_data.md) now have a separate
C++ decoder, checked against the 39 films. This establishes their fields and
events per frame, without building the live display list.

The transformation composition is ported to C++.
The [lifecycle rules](vmf_display_lifecycle.md) distinguish removal,
cache, rewind and destruction. The [C++ runtime](vmf_display_runtime.md)
carries these operations on live instances, with an explicit driver;
hooking up to the scheduler, the scripts and the
external services remains necessary before the menus can be played.

Still to be established separately: native loader/version/flags,
masks, hit testing,
geometry 2/46, text 10/11/37, sound 14/15, rate/pause integration and
keyboard/mouse/controller events. An interface displaying Xbox glyphs does
not on its own satisfy the reproduction of these behaviours.

## Reproducing the private evidence

- `analyze_vmf_v1.py` → `structure-report-v1.json`: envelope and hierarchy.
- `analyze_actions_v1.py` → `action-report-v1.json`: first check of the
  timelines only, kept without rewriting.
- `validate_vmf_v2.py` → `validated-report-v2.json`: conditions, actions,
  references and targets; `map-relations-v1.json` keeps the map witnesses.
- `header-crc-normalized-v1.json`: canonical recipe 39/39.
- `asset-name-links-v2.json`: image links 39/39; version 1 keeps an
  ID+2 hypothesis that was **refuted**; it is not a rule of the format.
- `readonly-audit-v1.json`: 39 banks and 39 private copies still identical
  after the analyses; no ISO opened by this study.
- `corpus-summary-v1.json`: cumulative counters and End/other targets; the
  documentary SHA it keeps is the one from its creation, before the final
  review clarifications.

The outputs are created exclusively and the previous ones preserved. Do not
commit these resources, banks, PDFs or large reports. No new native Rea
session was opened for this study; the existing analysis session remains the
only one.
