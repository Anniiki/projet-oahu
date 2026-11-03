# Official materials and textures in the x64 D3D12 pipeline

## Established wiring

The `tdur_view_mesh` CLI now links, with the project's C++ code:

`3DD instance → MBNK → MAT → PAR type2 → LAY ordinal → BMAP key → RGBA8 mips → D3D12 draw`.

PRIM/MBNK selection follows the [material evidence](retail_material_bindings.md).
The [texture owner](material_texture_bindings.md) keeps the caches shared
between LAYs. The [native pipeline](d3d12_textured_draws.md) receives the
geometry, the UVs and owned resources; no D3D9 command is translated.
Commercial files are opened read-only.

The inspection shader samples **one explicitly selected stage**, with white
vertex color and no added lighting. This does not reconstitute the original
program nor the composition of all its stages. The stage is not automatically
named "albedo" from its position or from a label.

## Explicit selection

```powershell
.\build\tests\render\Release\tdur_view_mesh.exe --bank "<installation>\Euro\Bnk\Vehicules\Alfa_GT.bnk" --member 1 --scene-member 0 --node 41:0 --material-member 19 --texture-stage 0 --texture-member 4 --only-material 5041494e545f4c52 --headless --output-bmp "<output-directory>\new-render.bmp"
```

- `--texture-member` is repeatable; its order defines the loaded collection.
  Integer BMAP keys, not file names, perform the matching.
- `--only-material` selects a raw key of 16 hexadecimal digits. Discarded
  primitives are counted and do not alter the framing.
- Without this filter, every primitive of the selected instances must resolve
  its stage. An unknown fallback, or a stage keeping a previous texture,
  triggers an explicit refusal; no substitute is invented.
- BORDER requires `--border-color R,G,B,A`. An explicitly supplied inspection
  color does not become an inferred native color. Cubes remain refused.
- All declared 2D mips are decoded and uploaded, within the budgets of the
  collection and of the backend. UVs remain unchanged.

The JSON report keeps the instances, primitives, materials, BMAP keys,
sampler indices and loaded members. `game_executed` and
`original_shaders_reconstructed` remain false.

## Fixed regressions

A valid black texture can have zero RGB coverage. The CLI measures geometric
coverage separately with a white pass, without modifying the requested image.
Framing is computed after filtering, only from the retained primitives, from
the composed matrices and the positive W.

A source-owned bank of **5 312 bytes** checks both cases: A alone, and A with a
very distant but filtered B, produce the same black image, RGB coverage 0,
geometric coverage **4 260**, fingerprint `60e1021eed6c2325`.
All **nine cases** pass on hardware and WARP, including refusal of unknown
stage/type/key and preservation of existing files. Two repetitions in the
same `--work-parent` create distinct batches; the 31 files of the first remain
unchanged. Private evidence: `<private-evidence>/reprise-2026-10-30/mesh-material-cli-cpp/`.

## Trials on the official banks

The six readbacks complete without launching the commercial game; RX6950XT GPU
and WARP, **64-bit** process, size 512, source `.3DD` 0 / `.3DG` 1.
A single `PAINT_LR` material and its stage0 are selected per case.

| Bank | Node:entry | 2DM member | 2DB member | Triangles drawn | Geometric coverage HW/WARP |
| --- | --- | ---: | ---: | ---: | ---: |
| Alfa_GT | 41:0 | 19 | 4 | 1 250 | 34 750 / 34 750 |
| 300_C | 45:0 | 25 | 15 | 1 146 | 35 641 / 35 641 |
| A3_V6 | 43:0 | 24 | 7 | 1 570 | 37 790 / 37 790 |

Each case confirms the exact MAT key `5041494e545f4c52` and BMAP key
`424f44595f4c5200`, along with a valid zero token. The banks have identical
SHA before/after. The BMPs of our framebuffer were inspected visually:
textured bodies, with the surfaces present in this single material. Wheels,
glazing, other materials and original shaders are not added by this
selection.

Geometric coverages agree; **the actual HW/WARP pixels are not identical**.
The seven extreme RGB differences (1/4/2 pixels) correspond to two adjacent,
overlapping, non-coplanar faces whose discontinuous UVs reproduce the two
observed colors. Their CPU depths are separated by 0.425 to 2.945 D32 ULP,
near their shared edge. This supports a different visibility choice under
CullNone/LESS, without proving the drivers' exact computations. The other 303
RGBA differences above eight remain unattributed. No global one-UNORM-step
tolerance nor native visual equivalence is claimed. The independent CPU
audit, without a new render, is kept in `real-material-pixel-differences/`;
the backend fixture tolerances keep their own scope.

The `native-textured-draws-cpp/parent-integration-v1/` batch keeps commands,
reports, BMPs, fingerprints and differences. Explicit target construction and
**46/46 x64 CTests** pass (9.15 s). The D3D12 validation layer is
unavailable: `validation_errors:null`. These results establish a resource
wiring and an inspection pipeline, without gameplay, complete vehicle or
multiplayer session.
