# Direct D3D12: draw batches and transformations

## Scope

C++ extension of the project's own native pipeline, without D3D9 translation.
`MeshViewer` can now draw multiple geometry ranges in one frame, each with
its local-to-clip matrix. This foundation selects no LOD, material or 3DD
node and is not the game's complete renderer.

## Interface and execution

`MeshDraw` contains `first_index`, `index_count` and a row-major matrix for
row vectors, identity by default. Indices address the buffers already
loaded by `upload`. Multiple draws can reuse the same range with different
transformations, without copying its vertices.

The `render_readback` and `present` overloads take a span of these draws.
They clear the color and D32 depth once, then update root constants and
issue one `DrawIndexedInstanced` per draw. These are multiple commands with
one instance each, without claiming to deliver GPU-instanced batching.
They all share the same depth test. The previous single-matrix methods
remain compatible.

The entire batch is validated before recording: at most 100 000 draws,
finite matrices, nonempty triangle-aligned ranges, and the 64-bit
offset+count sum within the buffer. An empty batch produces the cleared
background if geometry is loaded. Operations remain synchronous and wait
for their fences; the final client must still organize frames in flight.

## Validations

x64 MSVC build with `/std:c++latest /W4 /WX /permissive-`, without relaxing
the settings. RX 6950 XT and WARP each pass:

- Eleven RGBA readbacks, including equality between a single draw and two
  ranges, order invariance and a depth displacement that changes occlusion.
- Two independent translations, then two occurrences of the same range,
  with red/blue/background pixels verified at specified positions.
- Empty batch and twelve invalid operations in total, including an invalid
  second draw; rejection leaves the list inactive and the next render intact.
- Two presentations to a hidden fixture-owned window, with resizing from
  128×128 to 193×91. No actual on-screen visibility is inferred from these
  hidden presentations.

All 33 x64 CTests pass after integration. Regression on the official
`Alfa_GT/BODY_HR` group gives 3 781 vertices, 4 034 triangles, 25 425 covered
pixels and RGBA fingerprint `f39aba0e488f8dc1`. Its bank checksum remains
unchanged. No 3DD transformation or original material is applied yet.

The D3D12 debug layer is unavailable: admission rejected with `0x887A002D`.
Normal trials pass; their validation counters remain explicitly `null`,
without claiming a check by this layer. Private evidence:
`<private-evidence>/reprise-2026-10-30/native-scene-draws-cpp/`, `validation-v2.json`
and logs. No commercial game was launched.
