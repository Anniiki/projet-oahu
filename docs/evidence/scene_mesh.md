# Explicit 3DD/3DG assembly in C++ and D3D12

## Data and selection

`SceneMesh` assembles official resources read through `BankArchive`, without
calling the legacy engine. The result holds vertices, available UVs, indices,
primitive ranges and composed matrices. Ancestors without geometry are kept
in the transformations. A GEOM geometry is copied once, even when several
instances reuse it. Complete keys and primitive counts must match the 3DD
references.

`build_selected` receives a node/object-entry pair. A node with several
entries requires an explicit choice; the first entry is not treated as the
best LOD. The `build` shortcut with node indices accepts only unambiguous
cases. Descriptors remain opaque. A missing MBNK remains explicit and does
not become an assumed zero bank.

Limits apply to quantities, estimated logical content and the number of
vertices transformed for the bounds. They exclude excess vector capacity,
the allocator and parser intermediates. The complete 3DG file must be
handled, including its unselected groups. Bounds divide by W without erasing
affine residuals; non-positive W and overflows are refused. No material or
texture choice is invented.

## Direct D3D12 wiring

The viewer loads the project's own buffers, composes each matrix with its
inspection camera and emits the D3D12 draw batches. Source axes, indices and
transformations are preserved. Its uniform color and flat lighting serve to check
shapes; the original appearance, external wheels, materials, visibility and
dynamic LODs remain to be wired. This program is not a playable game client.

```powershell
.\build\tests\render\Release\tdur_view_mesh.exe --bank "<installation>\Euro\Bnk\Vehicules\Alfa_GT.bnk" --member 1 --scene-member 0 --list-nodes
.\build\tests\render\Release\tdur_view_mesh.exe --bank "<installation>\Euro\Bnk\Vehicules\Alfa_GT.bnk" --member 1 --scene-member 0 --node 38:0 --node 12:0 --node 18:0 --node 0:0 --node 1:0 --headless
```

`--node index:entry` selects a single variant per node. The JSON reports
the choices and distinguishes unique triangles from drawn triangles. The
optional `--output-bmp <new-file>` export writes only this renderer's
readback, with exclusive creation: no existing file is replaced. It captures
neither the official game nor its interface.

## Evidence and validations

MSVC C++latest `/W4 /WX /permissive-`; **36/36 x64 CTests** pass. The 3DD
readers and the assembly pass **1 079 and 62 checks** respectively on x64
and Win32 with the three banks. Fixtures include noncommutative composition
order, shared references, reindexing of two GEOMs, UVs, entry choice,
zero/four/five entries, missing REAL, missing MBNK, budgets and W. The
independent review finds no P1/P2 defect.

The RX 6950 XT GPU and WARP each pass six readbacks at predetermined pixels:
two objects, reversed order, single selection, shared geometry and explicit
replacement of a variant. Fixture colors are test-specific markers. The
D3D12 debug layer is unavailable; no validation by that layer is claimed.

Three official assemblies choose body, doors and mirrors explicitly. A3_V6
uses mirror entry 1 for RV_HR; the other two banks use entry 0. The renders
yield 28 338/35 962/38 785 covered pixels. The three banks keep their
SHA-256 hashes; nine CLI refusals include the existing BMP file, whose hash remains
unchanged. The historical local group BODY_HR keeps its fingerprint
`f39aba0e488f8dc1`.

Private evidence: `scene-mesh-cpp/official-assembly-v2/`, logs
`final-*-cpu-tests-v3.log`, `root-ctest-v3.log` and the
`rea/material-bindings/` study. No commercial asset is committed and no game
executable is launched by these validations.
