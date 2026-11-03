# Observed grammar of PC `.2DM` materials

## Corpus, identity and method

This survey describes the three official members already identified in
`<installation>/Euro/Bnk/Vehicules/`. It complements
[the material binding evidence](retail_material_bindings.md) without assigning
any shader, texture or sampler to the fields that remain opaque.

Indices are **physical, zero-based**, within the KNAB bank. The BNK offset
designates the start of the payload; every other offset in this document is
counted from the start of the `.2DM` member. Sizes include headers.

| Bank | Index | BNK offset | Member size | MAT/PAR/LAY |
| --- | ---: | ---: | ---: | ---: |
| `Alfa_GT.bnk` | 19 | `0x18A060` | `0x7860` / 30 816 | 34 / 34 / 34 |
| `300_C.bnk` | 25 | `0x2948E0` | `0x83A0` / 33 696 | 37 / 37 / 37 |
| `A3_V6.bnk` | 24 | `0x238630` | `0x8320` / 33 568 | 37 / 37 / 37 |

SHA-256 of the **`.2DM` members**:

- Alfa: `4555bb2b830a08104e17767ed9996450bec1ec430012c898d8b556651f8a6557`.
- 300_C: `8f82f6a38740a84eed3c82c413d53853ed3304c4e5ded6e06d7bde212b004d07`.
- A3: `bc9fd52b333bd46f291e547c5532bf4706f8c6652f66d49575e7ff268000584d`.

The existing JSON files `rea/material-bindings/asset-samples.json` and
`material-layout-references.json` were read before any new read. This
supplementary survey opens only the three payloads at the known offsets and
sizes; no other member is searched for or decoded. The SHA-256 digests of
the complete banks, referenced in the previous study, are identical before
and after.

Reproducible private batch:
`<private-evidence>/reprise-2026-10-30/material-2dm-layout/` contains
`inventory_2dm.py`, `inventory.json` and `method.json`. The JSON keeps the
headers, raw keys, REAL positions and typed edges without saving any
retail payload. SHA-256 of the inventory:
`a70752349eb0b0dd3f48f5a9415b9b9af3015a8b77f426191f85338b5e2138cf`.

## Header, chunks and physical order

All interpreted numbers are little-endian. The file header occupies
16 bytes: DWORD version `2` at `+0`, opaque zero DWORD at `+4`, total size
at `+8`, then the four bytes `.2DM` at `+0xC`.

A chunk header is also 16 bytes: four-byte tag at `+0`, opaque DWORD at
`+4`, inclusive size at `+8`, layout word at `+0xC`. All observed chunks are
aligned and sized in multiples of 16. Their opaque DWORD is zero. Containers
have layout `0x10`; the leaves `HASH`, `MAT.`, `PAR.`, `LAY.` and `REAL`
repeat their size in that field. Do not apply the container layout to the
leaves.

```text
.2DM, version 2
  LAYA : LAY. × N                     no HASH
  PARA : PAR. × N                     no HASH
  MATA : HASH, then MAT. × N
  MTPA : empty in this corpus
  UVAA : empty in this corpus
  REAL : list of field positions to relocate
```

Roots in their exact order; each cell gives `offset / size`, in
hexadecimal. Layout `10` for the first five, size for REAL.

| Tag | Alfa_GT | 300_C | A3_V6 |
| --- | --- | --- | --- |
| LAYA | `0010 / 2650` | `0010 / 29B0` | `0010 / 29B0` |
| PARA | `2660 / 2E10` | `29C0 / 32D0` | `29C0 / 3250` |
| MATA | `5470 / 2220` | `5C90 / 2520` | `5C10 / 2520` |
| MTPA | `7690 / 0010` | `81B0 / 0010` | `8130 / 0010` |
| UVAA | `76A0 / 0010` | `81C0 / 0010` | `8140 / 0010` |
| REAL | `76B0 / 01B0` | `81D0 / 01D0` | `8150 / 01D0` |

There is neither a gap between these roots nor data after REAL. The roles of
non-empty MTPA/UVAA are not established by their empty occurrences.

## HASH and declared records

Only MATA contains a HASH. Its size is `0x10 + 16*N`: `0x230` for Alfa and
`0x260` for the other two. Each entry is **16 bytes**:

| Offset within the entry | Established content |
| --- | --- |
| `+0..+7` | Raw resource key of **eight bytes** |
| `+8` | DWORD absolute offset within the member, to the start of a `MAT.` chunk |
| `+C` | Opaque DWORD, zero in all three files |

The whole key is used for bindings; do not truncate at the first NUL,
convert to a file name or assume UTF-8. HASH keys and targets are unique in
this corpus. The HASH covers every declared MAT, but **its order differs
from the physical order of the MATs in all three cases**. LAYA and PARA
resolve through declared offsets, with no invented name table.

| Record | Inclusive size | Fields interpreted here |
| --- | --- | --- |
| `MAT.` | always `0xF0` | PAR reference at `+E0` |
| `PAR.` | variable, table below | LAY reference at `+54` |
| `LAY.` | always `0x120` | Header only; payload kept opaque |

Distribution of PAR sizes:

| Size | Alfa_GT | 300_C | A3_V6 |
| --- | ---: | ---: | ---: |
| `0x60` | 1 | 1 | 1 |
| `0x80` | 2 | 2 | 2 |
| `0xA0` | 4 | 4 | 4 |
| `0x100` | 1 | 1 | 1 |
| `0x180` | 9 | 10 | 14 |
| `0x1A0` | 17 | 19 | 15 |

These sizes can be written arithmetically as `0x60 + k*0x20` in the
examples; this establishes neither a semantics nor a parameter count for k.

MAT keeps `+10..+DF` and `+E4..+EF` opaque. PAR keeps `+10..+53` and
`+58..end` opaque. LAY keeps its payload `+10..+11F`; eight physical 32-byte
windows can be identified from `+20`, but their roles and their selection
are not deduced from this geometry. The classification words, parameters
and identifiers present in these ranges are not shader types proven by this
survey.

## Typed references and REAL

Both fields use **chunk bases, headers included**:

```text
MATA.HASH.entry+8 → MAT. start
MAT.+E0          → PAR. start declared in PARA
PAR.+54          → LAY. start declared in LAYA
```

Example `GLASS\0\0\0` in Alfa: MAT `73C0` → PAR `4FB0` → LAY `2300`.
Do not add 16 to the targets, nor the BNK offset, before reading within the
member span. Both references in this example point backwards.

REAL contains DWORDs that are the **positions of the pointer fields**, not
their targets. After its 16-byte header:

| Bank | Non-zero DWORDs | Trailing zero DWORDs | Field classes |
| --- | ---: | ---: | --- |
| Alfa_GT | 102 | 2 | 34 HASH targets, 34 MAT+E0, 34 PAR+54 |
| 300_C | 111 | 1 | 37 of each class |
| A3_V6 | 111 | 1 | 37 of each class |

The REAL set is **exactly** the union of these three classes in the corpus.
Positions are unique and four-byte aligned, but **are not sorted**. For
example, the first three positions of Alfa are `5678`, `26C4`, `5790`. Do
not require ascending order or 16-byte alignment of the fields themselves.
The trailing zeros are REAL padding; they do not prove the existence of a
null MAT/PAR pointer.

All 324 non-zero fields surveyed point to the expected declared type and
start. No null MAT/PAR, PAR shared between MATs, LAY shared between PARs,
orphan record or cycle in the typed graph is observed. The known chain is
bounded to two edges after MAT; opaque words are not followed as pointers
on the grounds that their value looks valid. This map covers the file
payloads: it does not describe the fields that the engine may fill later in
memory. The absence of sharing or nulls in three files does not establish
that the original engine forbids them.

## Port guards, distinct from corpus facts

For the first supported C++ sub-format:

- Validate version, total size, root order, types, layouts and bounds of
  each parent before reading its children. A target record must be indexed
  by its exact start; an interior address or one of another type is
  refused. Do not search for any signature within a payload.
- Bound the file, chunks, materials, copied opaque bytes and outputs through
  configurable limits. Check additions and products before allocation; the
  34/37 counts are not a proven native ceiling.
- Validate each REAL position within the file, its four-byte alignment, its
  uniqueness and the trailing padding. Test set membership without depending
  on its order. All non-zero typed fields must be present in REAL. For this
  strict profile, an additional unknown REAL reference constitutes an
  unsupported layout, rather than a reason to continue arbitrarily.
- Do not replace a null pointer or an absent key with the first record. If
  the initial reader requires non-null MAT/PAR, present that refusal as an
  explicit limit of the sub-format; their native policy remains unknown.
- Any future alias support must keep the offsets and share the declared
  record. The observed bijection does not require inventing a native
  uniqueness rule for MAT→PAR or PAR→LAY targets. Duplicate HASH keys make
  lookup ambiguous and may be refused by explicit port policy.
- Another PAR, a non-empty MTPA/UVAA or another MAT/LAY size requires a
  documented extension of the sub-format, without automatically treating
  the other assets as corrupt. Keep the opaque fields and return an explicit
  refusal rather than a partially bound object.

These guards are not presented as checks recovered from a native routine.
The C++ reader and its fixtures are written separately. This step produced
only the private inventory and the present document: no game, probe, target
process or Rea session was involved; no personal key or account data is
read.
