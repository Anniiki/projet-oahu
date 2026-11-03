# PC .3DG geometry: usable structured subformat

## Origin and scope

This independent C++ reader decodes `.3DG` members of the official banks in
memory, through `BankArchive`. No OpenTDU code, game binary or asset is
imported into the repository. The result owns its positions, its available
UVs and its triangle indices; the bank can then be released.

The associated installation is PC MC1.66A, a 16 797 696-byte executable,
SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
First bank tested: `Vehicules/Alfa_GT.bnk`, SHA-256
`a049fb59b554d41130e4297fd4ad0c65cc91352052b07fde167329cf1a09e912`.
Its physical member 1, `Alfa_GT.3DG`, contains 473 520 bytes.

Primary reference consulted on GitHub:
[Durik256's Noesis importer](https://github.com/Durik256/Noesis-Plugins/blob/62f052895bbddba980e215e1a9b20052f6821ce7/fmt_3DG.py),
which identifies `.3DG`, the PRIM references, the float3 positions, the
ushort indices and the strip. This module rewrites a structured and
bounded reader: it does not reuse the PRIM signature scan or the heuristic UV
computation of that importer. The TDUMT primary resource
[GeometryPieces.cs](https://github.com/djey47/tdumt/blob/e7b433db37df889f4ed99c5907370a75a66fffd1/tdumt-lib/labs/WorldEditor/GeometryPieces.cs)
corroborates the 64-byte `GUN TIME` prefix after the GPU block header.

The supplied routine index export does not provide a reliably identified
`.3DG`/vertex routine. No original code at a given address is therefore
claimed as reconstructed. The evidence for this step associates the format
references with the structures of the official payloads. Support for the other
subformats awaits their own evidence.

## Structure actually read

All interpreted fields are little-endian. The retained subformat has the raw
initial DWORD `0x00030003`, named "3.3" here, a total size at `+0x08` and
`.3DG` at `+0x0c`. Blocks start after the 16-byte header.

Each block has a four-byte identifier, an opaque DWORD, its inclusive size at
`+8` and a last DWORD. For the supported containers, this last DWORD is 16;
for the leaves in use, it repeats their size. Sizes are multiples of 16 and
cannot exceed their parent.

The pipeline does not search for any signature within the vertex bytes:

```text
.3DG
  GEOA
    HASH : eight-byte keys -> absolute GEOM offsets
    GEOM : groups, each containing its PRIMs
  VBA. : declared DXVB blocks
  IBA. : declared DXIB blocks
  MORA / REAL : bodies kept opaque, not applied
```

A HASH entry is 16 bytes: an eight-byte key, a GEOM offset and an opaque
DWORD. Its references must point exactly at GEOM block starts. Keys may be
truncated or contain non-ASCII bytes; they are not guaranteed UTF-8 names.

| Offset relative to PRIM | Information used |
| --- | --- |
| `0x14` | Value 4 in the retained strip subformat |
| `0x18`, `0x1c` | Absolute offsets of the DXIB and DXVB blocks |
| `0x28` | Raw binding DWORD, with no claimed material semantics |
| `0x30`, `0x38` | Vertex/index starts: only the zero cases are accepted |
| `0x34`, `0x3c` | Counts of vertices and of ushort indices |
| `0x40` | Position stream declaration, equal to 1 |
| `0x44`, `0x48` | Normal/color declaration words, not decoded |
| `0x50` | `0xc01` or `0xc02` for the retained float2 UV cases |
| `0x88...` | Offsets of the streams present in the DXVB payload |

DXVB and DXIB have an 80-byte prefix: a 16-byte block header, then a 64-byte
prefix starting with `GUN TIME`. Positions are the first stream, three
consecutive float32 per vertex; indices are ushort. Both references must
point at blocks of the right container and the right type, never at a
signature found elsewhere in the file.

The first UV is exposed only for the observed declarations: normal
`0x900/0x901`, color `0x200/0x201`, null next declaration and UV
`0xc01/0xc02`. Its offset is read from the explicit stream table, after the
position and the optional normal/color streams. Its float2 range must lie
outside the positions and entirely within DXVB. The other cases yield an
empty UV vector; no unknown normal, tangent or UV format is guessed. The
second UV set of `0xc02` is not exposed.

## Triangles, coordinates and still unknown links

The reader converts strips into uint32 triangle lists local to each
primitive. It alternates parity even through degenerates, omits triangles
with identical indices and checks each index before any vertex access. The
`0xffff` marker resets the history and the parity. This last case is covered
by the fixtures and the Noesis/Xbox primary format reference; see the
[authors' exchange on strips](https://reshax.com/topic/1831-test-drive-unlimited-xbox-360-bnk-files/).
No `0xffff` was found in the 3 879 accepted PC members during this audit. It
is not an observed original PC branch.

Positions and UVs keep the source axes, order and values. No UV flip, unit
change, normalization, object transform or assumed identity matrix is
applied. `bounds` is computed from the positions of each primitive; it serves
to frame the viewer, not to place a piece in the world.

The PRIM DWORD `+0x28` is kept in `opaque_binding_word`. Its variations are
not enough to prove a material identity. The decoding and the binding with
`.2DM`, as well as the hierarchy/transforms associated with the model and
with the `.3DD` member, remain to be established. A monochrome display of
the `BODY_HR` group therefore proves neither the assembled vehicle nor its
in-game materials.

## Interface for the new engine

`Geometry3DG::parse(span<const byte>, limits)` returns `expected` and owns
all the decoded vectors. `groups()` exposes the groups and their primitives:
`array<float,3>` positions, optional first UV `array<float,2>`, uint32
triangles, bounds and source offsets. No D3D12 type is required.

For a first demonstration, select the raw ASCII name `BODY_HR` in
`Alfa_GT.3DG`, draw its primitives with diagnostic colors and frame the
union of their bounds. The camera choice and the disabling of culling belong
to the viewer; they are not reconstructed properties of the original scene.

Errors are explicit with code/offset/message. The default limits are 256 MiB
per file, 10 000 groups, 100 000 primitives/blocks, one million vertices and
three million indices per primitive, then 256 MiB of decoded
vertex/UV/triangle buffers. NaN and infinities are refused. The structure
counters bound the metadata allocations separately.

## Measurements and validations performed

MSVC x64 compilation succeeds with `/std:c++latest /W4 /WX /permissive-
/utf-8`. The standalone test passes **630 checks**: square with constant
expected triangles, UVs not bounded to `[0,1]`, normal/color variations, data
ownership, absent/unknown UVs, strip parity and degenerates, restarts, every
truncation of the synthetic file, references, sizes, indices, NaN/infinities
and budgets.

The pass over 5 003 banks finds **7 310 `.3DG` members**. The subformat
accepts **3 879** members: 9 939 groups, 27 076 primitives, 9 264 235
positions, 8 317 703 triangles and 4 982 032 first UVs. The observed maximum
is 18 852 positions per primitive. **59 788 checks pass**; this result
includes the checks of every emitted index but does not mean that every model
of the game is supported.

The **3 431 refusals** are limits of the reader, not a statement that the
official data is corrupt:

- 2 957 members have a buffer prefix other than the retained DXVB/DXIB,
  including `Level/Hawai/Area-0/Area-0-0/Area-0-0.bnk`; this subformat is not
  interpreted.
- 340 have an uncovered PRIM shape, including avatar resources from
  `COMMON_CLOTHES.BNK`.
- 130 have other GEOA children, including `Avatar/BARB.BNK`.
- Four have another version word, including the geometry of
  `Vehicules/Rim/Audi/A3_F_01.bnk`.

Precise samples: `Alfa_GT.3DG` has 34 groups and 66 primitives. `BODY_HR`
contains 12 primitives, 3 781 positions and 4 034 triangles; `BODY_LR` has
two primitives, 2 102 positions and 1 300 triangles. `Interior/GPH_L1A.bnk`
provides 33 primitives, 13 994 positions and 7 858 triangles. The test of
these two models and of an explicitly refused version (`A3_F_01`) passes
**833 checks**.

```powershell
.\build\geometry-local\geometry_3dg_test.exe
.\build\geometry-local\geometry_3dg_test.exe --official-root '<installation>\Euro\Bnk'
.\build\geometry-local\geometry_3dg_test.exe '<installation>\Euro\Bnk\Vehicules\Alfa_GT.bnk' '<installation>\Euro\Bnk\Vehicules\Rim\Audi\A3_F_01.bnk' '<installation>\Euro\Bnk\Interior\GPH_L1A.bnk'
```

Local logs: `build/geometry-local/official-corpus.log` and
`official-samples.log`. No game was launched for these validations and no
asset was written. The native rendering of the model is a later validation,
distinct from the decoding described here.
