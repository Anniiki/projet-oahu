# Bounded cache of the last D3D12 sampler layout

## Result and scope

`MeshViewer` retains the last layout instead of rebuilding vectors,
descriptors and heap for every identical presentation. The historical
constructor remains available; no change to NativeDevice, the quarantine,
TextureViewer, the shaders or the CLIs is required.
This code belongs to the native inspection pipeline written for this project.
It does not reconstruct the original graphics programs and does not prove the
smoothness of the complete game.

## Key and validation

The key contains the number and order of draws, the presence of a texture,
then the existing `MeshSampler` equality for each textured draw. No padding
hash and no approximate float comparison. Unused samplers of color-only
(untextured) draws are not part of the key, but remain validated.

Texture numbers, matrices, index ranges and lighting are not part of this key.
Their validation and binding are still executed: a hit reuses neither geometry,
nor matrix, nor texture from an earlier draw. Unknown enums, non-finite border
colors or border colors outside [0,1], invalid indices and missing textures
are rejected before preparation. The distinct-sampler budget is proven by the
already validated key on a hit, and recomputed before allocation on a miss.

The plan owns its data; no view into the caller's draws is stored.
`prepareSamplers` returns a constant reference and both call sites use
`const auto&`, avoiding a copy of the CPU indices.

## Bounds and transaction

A single layout is published. A reusable workspace prepares the candidate;
each buffer holds at most **100 000 uint32 indices**, each array at most
**256 samplers**. The two CPU buffers represent at most 800 000 bytes of
indices, excluding fixed objects and arrays. An overflow is rejected before any
growth or recording. This workspace is not a second queryable GPU layout;
`A → B → A` causes two misses and two new heaps.

A miss checks the cardinality in a fixed array, grows its workspace only if
necessary, creates a new heap and writes its samplers. The publication swap
happens after success. An input rejection, a CPU memory shortage, a failing
HRESULT or an incompatible factory output keeps the published plan. The
workspace and the counters may reflect a rejected attempt.

The generation indicates a **layout preparation** commit, not a successful
frame or a GPU completion. A later target allocation or presentation error
does not invalidate this prepared plan and does not promise its rollback.
An empty or entirely color-only batch keeps the default sampler heap and the
two-heap binding of the previous backend.

## API and lifetime

- `prepare_sampler_layout(draws)` warms up the same path after validation,
  without calling NativeSubmission, recording commands or waiting for the GPU.
- `sampler_cache_metrics()` returns cumulative counters on the owner thread;
  it is not a cross-thread synchronization mechanism.
- A constructor overload receives a `shared_ptr<MeshSamplerHeapFactory>`
  owned by the viewer. The existing constructor forwards directly to
  `ID3D12Device::CreateDescriptorHeap`.

The factory is called only on a miss, on the CPU side, without a GPU callback or
a back-pointer to NativeDevice. Its contract requires a **new** shader-visible
SAMPLER heap, matching the requested count, on the same device, without
reentry. The code rejects an incorrect HRESULT, S_OK/null, incompatible
type/flags/size, a device with a different COM identity and an alias of the
current heap before any sampler write. A custom factory must also respect the
prohibition on recycling an older heap; no unbounded historical registry is
added.

`GetDesc` reports NodeMask=1 for a creation with NodeMask=0 on our two
single-node devices. Only this normalization is accepted. A private
micro-fixture retains the tuples; Microsoft prescribes NodeMask=0 on a single
adapter.
[D3D12_DESCRIPTOR_HEAP_DESC](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_descriptor_heap_desc).

Each draw already retains its heap in the submission before the commands.
A new layout never rewrites the one in flight; the cache's last reference may
disappear while the tickets still own it. The asynchronous and quarantine
rules remain those of
[the native presentation](d3d12_async_present.md).

## Counter definitions

| Field | Meaning |
|---|---|
| `lookups`, `hits`, `misses` | Lookups after per-draw validation; a miss preparation can still fail. |
| `index_storage_allocations` | Successful CPU growths of the index buffer, not every allocation of a frame. |
| `heap_creation_attempts` | Calls at the factory boundary; an injection factory can reject without calling D3D12. |
| `heap_creations` | Heaps received and accepted after contract validation. |
| `sampler_writes` | Actual calls to CreateSampler, a method without HRESULT. |
| `generation` | Complete publications; unchanged on hit/rejection. |
| `cached_*`, `workspace_index_capacity` | Size/desc of the current plan and bounded CPU capacities. |

There is no GPU time field and no promise of removing every allocation of the
presentation path.

## Source-owned evidence

Private batch: `<private-evidence>/reprise-2026-10-30/sampler-layout-cache-cpp/`.
`validate.py`, `validation-final.json`, logs, fingerprint listings and
generated RGBA pixels. x64 Release build, MSVC 19.44, SDK 10.0.26100.0,
`/std:c++latest /W4 /WX`. This `/std:c++latest` language-standard mode is
still a draft.

**15/15 CTests pass**, including **93 cache checks per adapter** on RX 6950 XT
hardware and WARP. The existing async, native, texture and depth tests and the
47 submission-cycle checks also pass. The new facts are:

- After a two-sampler warm-up: 256 hits with modified matrices, ranges and
  texture numbers, zero C++ allocation observed in the warm-up, zero new
  real CreateDescriptorHeap calls and zero CreateSampler calls.
- Actual misses and no LRU; late errors keeping the cache, 100 000 draws
  accepted, 100 001 and 257 distinct samplers rejected before
  allocation/recording; empty fallback kept.
- Eight factory outputs rejected: E_OUTOFMEMORY, S_OK/null, wrong type,
  non-visible heap, wrong size, other device, reuse of the current heap,
  S_FALSE. An actual CPU index growth fails through an injected `bad_alloc`,
  before GPU creation and without replacing the cache.
- A source-owned queue gate blocks an actual submission. The initial heap
  survives the cache replacement **and the destruction of the viewer**; the
  unused replacement heap is destroyed; the old one disappears after the
  proven fence. A two-second watchdog, unconditional release and join protect
  the exception exits. No device removal was triggered.

The witness attaches an IUnknown to the real heap with SetPrivateDataInterface:
its AddRef/Release follows the heap destruction without any COM facade passed
to the GPU.
[Microsoft contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12object-setprivatedatainterface).
The other-device test uses HW and WARP: two CreateDevice calls on the same
adapter may return the singleton, so they do not constitute this case.
[D3D12CreateDevice](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-d3d12createdevice).
This part of the fixture requires both adapters on its host.

A private copy of the previous renderer, source SHA-256
`0486b32c7fff5d97c423435ccdf0d0d490c5674fb9c0906260d01176074e3a0f`,
runs the same suite of 33 readbacks, without calling the new APIs.
The capture carries revision `c5497410`; this file is identical to the one in
`ec2ec9b`, common Git blob `48b94903f4b238d29f7b5171ecba34e839ceeb12`.
Before/after comparison **byte by byte**: **1 081 344 bytes, zero difference** for
both adapters together. The 33 synthetic images are also identical between
the two adapters on this workstation; this defines no universal tolerance for
real assets or other GPUs. No game, commercial asset or game process was
opened; the lifetime witness's only window remains hidden.

The D3D12 debug layer is not available in these results:
`validation_errors=null` does not mean zero attested errors. The allocation
oracle targets the C++ `operator new/new[]` of the warmed-up preparation only,
not the driver's private allocations or those of NativeSubmission.
Lookup and validation keep a linear CPU cost; a layout change can still
allocate a heap. Streaming, shaders, Present, complete frametimes and driving
stutter remain to be measured separately.

## Integration into the main repository

The `vmf-sampler-parent-integration-v1/` batch builds the 50 explicit
Release/x64 executable targets, then passes **53/53 CTest**, including the two
new tests `d3d12_sampler_cache_hardware` and `d3d12_sampler_cache_warp`.
Each of them reproduces the 93 checks, the 256 hits without new
C++/heap/sampler allocation in the measured preparation and the 33 readbacks.
The three cache sources have the same fingerprints as the final private batch.
The batch also retains the first 52/53 run and the fix for the SDL virtual
test bench, described in [its evidence](sdl_gamepads.md).
