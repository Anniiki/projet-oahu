# PC .3DD scene: references and hierarchy

## Source and scope

This independent reader takes official in-memory payloads from `BankArchive`.
No OpenTDU engine code or game pseudocode is imported. It handles the PC
version 2 subformat observed in three vehicle banks, without claiming
support for world scenes or every layout.

Primary reference: `Vehicules/Alfa_GT.bnk`, SHA-256
`a049fb59b554d41130e4297fd4ad0c65cc91352052b07fde167329cf1a09e912`.
Its physical member 0 starts at `0x3a0`, contains 29280 bytes and has SHA-256
`7d4b35fd68dbf22f2791c2a570f0993e5cd5570788dd6cf99ad538a4ba02143a`.
Member 1 is the associated geometry, already described in
geometry_3dg.md.

## Actually validated structure

Numbers are little-endian. The 16-byte header gives version 2 at `+0`,
total size at `+8` and `.3DD` at `+0xc`. Chunks have a four-byte identifier,
an opaque word, an inclusive size and a layout word. Their size is a
multiple of 16; no child may exceed its parent.

```text
.3DD
  MBNA : MBNK, material-key lists
  OBJA : HASH -> OBJ.
  INSA : HASH -> INS.
  HIEA : HASH -> HIE.
  REAL : locations of references to relocate
```

HASH entries are 16 bytes: a **raw eight-byte key**, absolute record offset
and uninterpreted DWORD. Keys are not UTF-8 text: some contain nonprintable
bytes. References must point to declared record starts, with unique
keys/targets. The reader searches for no signature within a payload.

| Record/field | Exposed reading |
| --- | --- |
| `OBJ.+0x1c` | Primitive count, corroborated with the associated GEOM |
| `INS.+0x10..0x4f` | Sixteen transformation float32 values |
| `INS.+0x54` | Absolute reference to an MBNK chunk |
| `INS.+0x5e` | unsigned16 object-entry count |
| `INS.+0x60+20*i` | Reference to an **OBJ HASH entry**, then four opaque DWORDs |
| `HIE.+0x14` | Reference to INS. |
| `HIE.+0x18` | Retained word; its low sixteen bits match the descendant count |
| `HIE.+0x1c` | Parent HIE., or zero for a root |

An MBNK entry is 16 bytes: a raw eight-byte key and eight retained opaque
bytes. Every interpreted nonzero reference must also appear in REAL.
Cycles, invalid parents, inconsistent descendant counts, nonfinite
matrices and exceeded budgets are rejected.

The reader retains all entries, in order, under
`Scene3DDNode::object_entries`. The `object_index` alias represents only
the first entry and identifies no selected LOD. The supported `0xb0`-byte
INS layout holds at most four twenty-byte entries; a larger count is
rejected before reading. Zero count: no object reference is followed.
Every active reference must appear in REAL. The four descriptors remain
opaque bits, never assumed pointers. A missing MBNK does not erase
geometry. The MBNK key count is not required to equal the primitive count:
this sample correlation is not the native indexing contract.

## Proven binding example in Alfa_GT

`BODY_HR` HIE. at `0x6470` points to INS. `0x3120`, with parent `D_CAR`
at `0x5df0`. `INS.+0x60` targets HASH entry `0x740`, whose raw key
`BODY_HR\0` identifies OBJ. `0xb60`. That object declares twelve primitives.
`INS.+0x54` targets MBNK `0x2b0`, containing twelve keys.

`resolve_geometry(raw_3dg)` requires the same **complete eight-byte key**
in the supplied geometry's GEOA HASH, as well as the same PRIM count.
For each `(node_index, object_entry_index)`, it returns GEOM offsets
compatible with `Geometry3DGGroup::source_offset`. Distinct instances can
thus share an object without having its name. Vertex/index decoding and
validation remain the separate responsibility of `Geometry3DG`, apart
from this reference resolution.

The PRIM DWORD at `+0x28` is retained raw. In `BODY_HR`, its high bits
successively equal 0 through 11; this agreement with the twelve MBNK keys
**does not establish the complete semantics of material assignment**.
No texture or shader is selected from that assumption.

The new Rea study establishes from the
secondary executable the unsigned16 `PRIM+0x2a` selector, fallbacks and
MBNK-to-MAT resolution. These routines still need corroboration in official
memory; the present reader still retains raw primitive bindings.

## Transformations and assembly limits

The reader retains the stored matrix and supplies an explicitly defined
composition for row vectors: `local * parent_composé`, without axis
conversion. Matrices must be finite and affine within `1e-4`, but small
stored residuals are not erased. Intermediate calculations use double,
then round to float; no bitwise equality with an original engine cache
is claimed.

Fixtures check noncommutative order with scale, rotation and translation
across three generations. In the official resource, `FORK_FL` stores
translation `(0.76975, 0.152931, 1.27813)`; parent references are followed
even when an ancestor has no geometry.

Scenes simultaneously contain multiple LODs, shadows and auxiliaries.
The reader exposes the graph and referenced MBNKs; it **does not decide
their activation**. Automatically drawing all 56 Alfa_GT instances is
not proof of a vehicle assembled with the game's rules.

Both mirrors in each bank have two entries. Alfa_GT and 300_C list RV_HR
then RV_LR; A3_V6 lists RV_LR then RV_HR. Source order and the four
descriptors are preserved. Neither sorting by suffix nor choosing the
first entry is an established LOD policy.

## Rea evidence and unknowns

Rea statically examines the official PE with SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Local evidence in `<private-evidence>/reprise-2026-10-30/rea/original-scene`:

- `search_bytes` finds `.3DD` at `0x00f12f58`, Evidence
  `ev_9204f872cc0f092f9d97df224c5f2523a7bb66911375fd7d5bcf2c8cf5745eb3`.
- `0x008ece80` constructs a `.3DD` path, Evidence
  `ev_a971500043a38f63c6d5a71c7ba87eec8acb502627fe5768ccea7128aa53a521`.
  This does not document the chunk reader.
- The `0x0050ad00` dossier, parent
  `ev_75583aef1da48dab2ee28b8d284776199de38e2228b08a4ecd8bd0a187ecc335`,
  and the pseudocode of `0x00504930` contain overlapping or truncated
  instructions. They establish neither LOD rules nor engine composition.

No routine at these addresses is declared reconstructed. Evidence for the
subformat rests on official bytes, matching references and structural
validations. This work launches no game, changes no protection and reads
no activation key.

## Validation

MSVC 19.44, C++ latest, `/W4 /WX /permissive-`: successful build.
1 079 checks pass with the three banks, covering truncations, interior
references, missing REAL, cycles, nonfinite/overflowing matrices, budgets,
aliases and strict pairing:

| Bank | Nodes | OBJ | MBNK | Nodes with an object | Entries linked to .3DG |
| --- | --- | --- | --- | --- | --- |
| Alfa_GT | 97 | 34 | 33 | 56 | 58 |
| 300_C | 101 | 36 | 35 | 59 | 61 |
| A3_V6 | 103 | 35 | 34 | 62 | 64 |

These new totals include every variant, unlike the earlier counters that
considered only `INS+0x60`. The independent Rea inventory is retained in
`rea/material-bindings/variant-bindings.json`.

All three banks retain their before/after SHA-256. Local reports:
`build-scene-3dd/official-scenes.txt` and `official-bank-sha256.json`.
After CMake integration, run `scene_3dd_test` without arguments for
fixtures, or with these bank paths: members 0/1 are then supplied
explicitly by the convention of the three official fixtures, without
automatically searching for other geometry. No commercial asset is committed.
