# Native renderer: goal and validation criteria

## Requested result

The reconstructed native Windows x64 C++ client must draw directly with
D3D12, without D3D9On12, DXVK, vkd3d or any other runtime translation of
D3D9 commands. Vulkan remains another selectable backend. Commercial
resources come from the player's local copy, opened read-only. OpenTDU
remains a secondary reference; its engine is not linked to this client.

The current bridge in the official-game mod serves observation and display
trials. It is not the final renderer. Loading `d3d12.dll` or presenting
through DXGI is insufficient to establish native rendering of the game.

## Established state

`NativeDevice` directly creates the D3D12 device, queue, buffers and
synchronization. Project-owned pipelines draw 2DB textures and 3DG groups,
with depth and resizing verified on GPU and WARP. The 3DD readers resolve
objects, instances and parent transformations. Initial material stages are
connected; their complete composition, LOD selection and world scenes
remain to be established. This foundation currently renders no gameplay.

[Draw batches](evidence/d3d12_draw_batches.md) now manage independent ranges
and transformations with shared depth and geometry reuse. The
3DD/3DG connection applies matrices to explicitly
selected instances and entries, verified on GPU and WARP. Dynamic LOD
selection and original materials remain required for the complete vehicle
assembly in step 2.

The `.2DM` reader now connects the 289
primitives of three vehicles to their MAT/PAR/LAY through the instances'
MBNK banks. Textures and samplers
are established for the type 2 parameter path, with code corroborated in
the official executable in memory. The D3D12 integration
now uses the UVs, mips and samplers of one explicit stage for three official
car bodies. Missing resources or unknown previous states are rejected.
External textures, original programs and stage combinations remain to be
reconstructed. The program-selection evidence
and [key calculation](evidence/resource_hash.md) establish part of the input
to the future variant system, without identifying every shader.

## Verifiable steps

1. Manage multiple objects and transformations per frame, then connect
   explicitly selected 3DD instances to their 3DG groups. Preserve ancestors
   without geometry. Do not draw all LODs simultaneously while claiming to
   implement the original rules.
2. Establish material, texture and vertex declaration bindings; assemble a
   vehicle exterior and interior using proven references. An inspection
   color does not replace the game's materials.
3. Reconstruct world loading/streaming, visibility, terrain, lighting,
   shadows, transparency, HDR and the interface from binary evidence.
4. Connect presentation to client states and interpolate the fixed-step
   simulation. Validate driving, menus and transitions without translating
   D3D9 calls.
5. Add the Vulkan backend behind the reconstructed data and commands,
   then verify the same scenes and behaviors with both choices.

## Final validation

A playable session must load the local copy, traverse the world, allow
driving and reproduce the expected interfaces and effects. Check dependencies
and the graphics-command path, then window modes, monitor changes, frame
limiting and behavior without focus. Physical VRR and simulation independence
from FPS require their own trials. Synthetic tests do not constitute these
validations.

Measure smoothness through CPU/GPU frame times, their percentiles and long
frames during driving, streaming and transitions. Prepare shaders and
resources outside the critical path, bound allocations and synchronize
CPU/GPU only where dependencies require it. Validated retention of failed
submissions is a lifetime prerequisite; it does not prove the absence of
stuttering in the final game.

The asynchronous presentation path removes
the unconditional wait from both visualization loops. Three submissions
may remain in flight; allocator reuse and readback/upload operations retain
their necessary waits. Telemetry separates wait calls, CPU scopes and GPU
timestamps from the witnesses. The integration passes 55/55 x64 CTests,
without yet validating complete-game frame times.

The last sampler-layout cache removes
recreation for an identical layout. Synthetic pixel readbacks remain
identical to the previous renderer, and the replaced heap survives the
viewer while its GPU submission remains in flight. Layout changes,
submission allocations and DXGI blocking retain their costs.
