# .3DG group viewer in D3D12

## Origin and scope

`MeshViewer` is an inspection facility written for TDU-Recompilation, with
HLSL shaders specific to the viewer. It reuses `NativeDevice` without any API
change and without depending on SDL, DXVK or the OpenTDU engine. The program
does not launch the game and does not claim to reconstruct its rendering.

`BankArchive` opens a PC KNAB bank in read-only mode. `Geometry3DG` reads the
structured path HASH → GEOM → PRIM → DXVB/DXIB documented in
[geometry_3dg.md](geometry_3dg.md). A single named group is selected; its
local positions and triangles are copied without any axis or winding
inversion. Concatenation only adjusts the indices of each primitive.

No .3DD transform, material association, texture or original normal is
invented. The uniform bluish color and the flat lighting from derivatives serve
to inspect the volume. Culling is disabled; this does not corroborate the face
conventions of the original engine. Displaying `BODY_HR` does not constitute
the assembly of a complete vehicle.

## Native pipeline and camera

The DEFAULT vertex and index buffers are filled through an UPLOAD buffer, with
explicit transitions to the vertex/index buffer states. The
[D3D12 input views](https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-descriptors)
describe float3 position, float4 color and uint32 indices. The SM 5.0 shaders
receive a row-major matrix for row vectors in the root constants.

An RGBA8 target and a D32_FLOAT depth buffer are cleared for every frame. The
[depth test](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_depth_stencil_desc)
uses LESS, writes depth and leaves the stencil disabled. The off-screen
rendering is copied into a READBACK buffer after a fence. DXGI presentation and
resizing use the same pipeline with a matching depth buffer.

The camera is an **explicit inspection camera**, centered on the bounds of the
group. It uses [XMMatrixLookAtLH](https://learn.microsoft.com/en-us/windows/win32/api/directxmath/nf-directxmath-xmmatrixlookatlh),
a vertical field of view of 45°, yaw 35° and pitch 20° by default. These
choices do not describe the game camera and do not modify the source positions.
Distance and clipping planes are derived from the bounding sphere, in the local
units of the file.

## Build and commands

```powershell
cmake -S tests/render -B build-render-device -G "Visual Studio 17 2022" -A x64
cmake --build build-render-device --config Release --target tdur_view_mesh tdur_d3d12_mesh_viewer_test --parallel 3
ctest --test-dir build-render-device -C Release -R "^d3d12_mesh_" --output-on-failure
.\build-render-device\Release\tdur_view_mesh.exe --bank "<installation>\Euro\Bnk\Vehicules\Alfa_GT.bnk" --member 1 --group BODY_HR --headless
```

Without `--headless`, the window carries the title
**TDU-Recompilation | Visualiseur 3DG D3D12**, for five seconds by default.
Options: `--group BODY_LR`, `--size 64..2048` for the off-screen readback,
`--seconds 1..60`, `--yaw -360..360`, `--pitch -80..80`, `--warp`, `--debug`.
The command fails explicitly if the group or the sub-format is not read.

## Validation on 30/10/2026

The MSVC 19.44.35229 build with `/std:c++latest /W4 /WX /permissive-`
succeeds. Two independent synthetic tests pass on the RX 6950 XT and on WARP:

- A red triangle near the camera, drawn before a distant blue triangle; red
  center, blue border, black background and a coverage of 6728 pixels within
  the expected range.
- Identical rendering when the order of the two triangles is reversed: the
  result depends on depth, not on the last triangle drawn.
- Depth preserved after a size change; a matrix translation moves the whole
  geometry outside the clip volume.
- Four GPU readbacks and six invalid operations refused per adapter, notably an
  out-of-range index and non-finite values.

Official member 1 of Alfa_GT is then validated off screen at 512 × 512:

| Group | Primitives | Vertices | Triangles | Covered pixels, hardware |
| --- | --- | --- | --- | --- |
| `BODY_HR` | 12 | 3781 | 4034 | 25425 |
| `BODY_LR` | 2 | 2102 | 1300 | 35310 |

WARP also produces 25425 covered pixels for `BODY_HR`. The RGBA fingerprints may
differ between adapters because of floating-point lighting computations; no
bit-for-bit equality between these renderings is assumed. The reports are kept
under `build-mesh-viewer/official-*.json`.

After these validations, a five-second visible run on hardware presents 801
frames of `BODY_HR`, in the 3DG viewer window. The report
`build-render-device/official-BODY_HR-visible.json` keeps this result and the
same off-screen coverage; no other part of the vehicle is assembled.

The bank retains the same SHA-256 before and after:
`a049fb59b554d41130e4297fd4ad0c65cc91352052b07fde167329cf1a09e912`.
Commercial resources remain outside Git. The D3D12 debug layer is absent
locally; these results prove the pixel readbacks and the occlusion, without
claiming an execution validated by that layer.
