# Native D3D12 textured draws

## Scope

`MeshViewer` remains an inspection pipeline written for this reconstruction.
It uses neither D3D9On12, nor OpenTDU code, nor any original shader. The
renderer knows no `.2DM`, `.2DB` or `.3DG` format: the host supplies geometry,
transforms, RGBA8 pixels and explicit sampler states. The first tests open no
commercial resource and launch no game.

## Public contract

- `MeshVertex` adds `texcoord` after `color`: position 0, color 12, UV 28,
  stride 36 bytes. All components must be finite.
- `MeshTextureLevel{width,height,pixels}` holds tightly packed RGBA8 rows,
  without sRGB, flipping, gamma conversion or premultiplication. Each
  following level halves both dimensions, with a minimum of 1. Partial chains
  are allowed; repeating a terminal level is refused.
- `upload_textures(span<MeshTexture>)` replaces the whole collection after
  success. The source spans may be released after return. An empty collection
  clears the textures without clearing the geometry.
- `MeshDraw::texture_index` selects a texture explicitly. An absent value
  keeps the existing colored draw. The shader multiplies vertex color and
  texture, then applies the inspection lighting when requested.
- `MeshSampler` separates MIN and MAG (`point`, `linear`), MIP (`none`,
  `point`, `linear`), U/V (`wrap`, `clamp`, `mirror`, `border`) and
  `border_rgba`. W remains clamp. Unknown enums and border components that
  are not finite or outside `[0,1]` are refused. `none` explicitly means LOD
  limited to 0.

The public budgets are **256 textures**, **15 levels per texture**,
**1 024 total levels**, **256 MiB of cumulative RGBA8 pixels**, **256 MiB of
cumulative padded staging**, and **256 distinct samplers per batch**. The
dimensions remain bounded by D3D12 and the image budget. The preexisting
budget of 100 000 draws still applies.

## Upload, states and lifetime

Full validation precedes texture allocations and command recording. The D3D12
footprints are computed for each mip; each source row is copied to its
RowPitch, regardless of its alignment. The temporary resources and their SRVs
are distinct from the old collection. They transition from COPY_DEST to
PIXEL_SHADER_RESOURCE, then a fence is submitted and waited on before they are
published. An input refusal keeps the previous collection and the command
recording state.

Each batch prepares a heap of deduplicated samplers before recording. Each
draw binds its SRV and its sampler; the draws share the same clear and the
same D32 depth. SRV slot 0 is null for colored rendering. The methods are
synchronous and promise no concurrent access to the same `NativeDevice`. Its
device or fence errors are propagated; recovery after GPU loss or timeout is
not validated by these tests.

### Faulted submission and retention

A review identified a defect in the old path: `ExecuteCommandLists` preceded
a Signal that could fail, then local temporaries could be destroyed without
proof of GPU completion. The new `NativeSubmission`, actually used by
NativeDevice, closes the batch once, then forbids any new submission after a
fault. NativeDevice exposes `faulted()`; its standard methods remain
available with the same signature, except `retain(IUnknown*)`, now strictly
before submit, which also covers heaps, root signature and PSO.

MeshViewer retains every object used before its commands: buffers, uploads,
textures, readback, depth/target, heaps and pipeline. The controller also
keeps the command list, the allocator and an adapter owning the COM
references of device/queue/fence as well as its wait event. No callback
references a NativeDevice that could be destroyed.

A slot is reserved **before Execute** in a registry bounded to **64 batches**,
with at most **4 096 distinct retained objects per batch**. Exhausted
capacity refuses the preparation. The registry releases executed objects only
after a completed fence, or a definitive device removal reason. A wait return
of S_OK, a timeout or `GetCompletedValue()==UINT64_MAX` alone is not enough.

An unproven completion transfers the retention to a logged quarantine,
readable with `NativeSubmission::quarantinedCount()` and drainable with
`drainQuarantined(0)` (the number of batches still in quarantine is returned).
The poll never re-executes Close/Execute; it may retry Signal and check the
fence. The objects **and their drain context** remain owned until that proof.
The registry persists without a static destructor that would release objects
still in use. Drained slots are actually released; this is not a silent leak
chosen instead of a check. The fault remains latched even after a successful
drain: no rendering recovery is claimed.
An allocation or capacity failure during retain also closes the preparation
and latches the fault. The destructor therefore does not submit the partially
retained batch; the objects of a batch never executed may be removed without a
fence.

## Standalone x64 validation

Private evidence:
`<private-evidence>/reprise-2026-10-30/native-textured-draws-cpp/`.
`validate.py` builds only NativeDevice, MeshViewer and two fixtures, with
**MSVC 19.44.35229.0**, SDK **10.0.26100.0**, **/std:c++latest /W4 /WX**.
The properties of the generated projects and the digests of the sources and
binaries are kept with the private logs.

**5/5 CTest pass**: the existing batch/depth fixture and the new textured
fixture, each on **AMD Radeon RX 6950 XT** and **Microsoft Basic Render
Driver/WARP**. The new fixture performs **20 959 checks** and **94 successful
readbacks per adapter**. It checks in particular:

- 4 096 asymmetric pixels, exact UV orientation and channel order;
- two textures and two samplers in one frame, reused geometry, permuted
  order, shared depth and the colored branch;
- descriptor replacement and independence from the source spans;
- WRAP/CLAMP/MIRROR/BORDER on U and V, positive and negative coordinates,
  RGBA border color, independent MIN and MAG filters;
- four real mips, LOD none, last-mip selection, linear MIP interpolation,
  partial chain and odd/non-square dimensions;
- refusal of invalid counts, dimensions, bytes, mips, padding, enums and
  indices; preservation of the old textures and absence of partial recording;
  validity of the last allowed SRV and of 256 distinct samplers.

Point pixels, the central linear MIN/MAG cases and the border states are
compared exactly. Trilinear blending at fractional LOD tolerates **one UNORM
step** for the precision of the LOD and filtering computation. The existing
depth test keeps exactly **6 728 covered pixels**.

The fifth test, `native_submission_lifecycle_test.cpp`, runs **47 checks
without a GPU** on the same controller with owned operations and witness
IUnknown objects. It injects Close failures, Signal failures after Execute,
wait timeouts and a false successful wait; it checks the absence of double
submission, the deferred destructions, the callback lifetime after the owner
disappears, then the late drains and proven device removals. The batch/object
capacity refusals occur before Execute. The two quarantine entries of this
test are synthetic and are drained before it ends; no fault of the real driver
is provoked.

The hardware and WARP attempts with `--debug` fail explicitly on the
unavailable D3D12 layer (`887A002D`). The runs without debug therefore publish
`validation_errors:null`; no zero count of validation errors is claimed.

## Integration and limits

The CMake and CLI integration is in place: **46/46 x64 CTest pass**, including
the hardware/WARP textured tests, the submission cycle and the black or
filtered material cases. `tdur_view_mesh` supplies the UVs, mips and samplers
produced by the project's readers. Six readbacks on three official banks are
described in [native_material_inspection.md](native_material_inspection.md).
The textured test accepts `--warp` and `--debug` when that layer is available.
All command list users must register their objects through `retain` before
`submit`; raw access to the queue does not provide that proof by itself.

This work provides neither original materials/shaders, nor reflections, cubes,
arrays, anisotropy, depth comparison, blending or sRGB. It proves no result of
the official game. Asset key binding and the material/LOD policy remain
separate responsibilities of the host.
