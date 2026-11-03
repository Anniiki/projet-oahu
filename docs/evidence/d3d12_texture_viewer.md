# Native D3D12 texture viewer

## Scope and source

`TextureViewer` is platform code written for TDU-Recompilation. It reuses
`NativeDevice` without modifying its API and imports no shader, DXVK binding
or OpenTDU engine code. It displays one selected resource, without rebuilding
a scene or running the game.

The original sources are read by `BankArchive` and `Texture2DBView`; see
[the .2DB format](texture_2db.md) and [the PC KNAB archives](bank_archive.md).
The bank remains the owner of the storage for the lifetime of the borrowed
views. One mip and one slice are decoded to RGBA8, with no gamma transform,
vertical flip, premultiplication or normal-map interpretation.

## GPU path

An UPLOAD buffer receives the rows according to the D3D12 footprints. A
DEFAULT texture then receives `CopyTextureRegion` and transitions from
`COPY_DEST` to `PIXEL_SHADER_RESOURCE`. This layout follows the
[Microsoft constraints on texture upload and readback](https://learn.microsoft.com/en-us/windows/win32/direct3d12/upload-and-readback-of-texture-data).

HLSL shaders specific to the viewer, compiled by D3DCompile as SM 5.0, draw
one triangle covering the target. An SRV and a point/clamp sampler perform
[SampleLevel at level zero](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-to-samplelevel).
Alpha blending and sRGB conversion are disabled. The off-screen rendering is
copied to a READBACK buffer after a barrier and a fence wait; every RGBA byte
is compared with the CPU decode. The window actually uses DXGI/D3D12.

The reader's BC1/BC2/BC3/BGRA8 formats are **decoded on the CPU** before the
RGBA8 upload: this version does not claim native upload of BC blocks. It
handles one image at a time and waits on fences synchronously, without
claiming to be an optimized streaming engine.

## Build and usage

```powershell
cmake -S tests/render -B build-render-device -G "Visual Studio 17 2022" -A x64
cmake --build build-render-device --config Release --target tdur_view_texture tdur_d3d12_texture_viewer_test --parallel 3
ctest --test-dir build-render-device -C Release -R "^d3d12_texture_" --output-on-failure
.\build-render-device\Release\tdur_view_texture.exe --bank "<installation>\Euro\Bnk\Vehicules\Alfa_GT.bnk" --member 4 --headless
```

Removing `--headless` presents the texture for five seconds in a window
titled **TDU-Recompilation | Visualiseur de ressource D3D12**.
Options: `--mip N`, `--slice N`, `--seconds 1..60`, `--warp`, `--debug`.
Slice selection keeps the stored order; the semantic orientation of cube
faces is not established. Archived paths are metadata, never extraction
destinations.

## Validation on 2026-10-30

MSVC 19.44.35229, C++ latest (`_MSVC_LANG=202400`), `/W4 /WX /permissive-`:
the build succeeds. The latest mode is that of the available compiler; it
does not mean a complete C++26 implementation.

- Two off-screen tests pass on AMD Radeon RX 6950 XT and WARP: four uploads,
  twelve renders and 36792 RGBA bytes verified exactly per adapter. The
  independent patterns cover alpha, orientation, unaligned pitch,
  magnification, minification and texture replacement. Six invalid
  operations are refused.
- Three official Alfa_GT members were rendered and read back on hardware:

| Index | Member | Stored format | Image | FNV-1a64 RGBA CPU = GPU |
| --- | --- | --- | --- | --- |
| 4 | `body_lr.2db` | BC3 | 256 × 256 | `bc17b2992d0d7a68` |
| 10 | `lgt_f_g.2db` | BC1 | 8 × 8 | `e613e43582ac02a5` |
| 12 | `ws_logo.2db` | BGRA8 | 128 × 128 | `d17c6bc1cfa3e6f5` |

Member 4 is also verified off-screen on WARP. A visible five-second trial on
hardware presents 802 frames; its 262144 RGBA bytes still match the decode.
This presentation count is not a measurement of game performance.

The bank's SHA-256 is identical before and after:
`a049fb59b554d41130e4297fd4ad0c65cc91352052b07fde167329cf1a09e912`.
Local evidence: `build-render-device/official-body-lr-visible.json`,
`official-bank-integrity.json`, CTest logs and the reports of the three
members under `build-texture-viewer/`. No commercial file is included in Git.

The D3D12 debug layer is absent from the workstation: `--debug` fails
explicitly with `0x887A002D`. The pixel comparisons pass; no "zero D3D12
validation errors" result is claimed without that layer.
