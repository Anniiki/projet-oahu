# D3D12 presentation with several frames in flight

## Scope and API

Platform code written for the independent client, without D3D9On12 or OpenTDU
source. This change removes a systematic CPU synchronization from the two
viewers; it proves neither the absence of stutter nor the operation of the
reconstructed game. The audit starts from backend `a18185c`.

`MeshViewer::present_async(transform|draws, inspectionLighting, vsync)` and
`TextureViewer::present_async(vsync)` return a `uint64_t` render fence.
They record, submit and call DXGI Present without waiting for that fence.
A completed fence does not mean that the screen has displayed the image. The
former `present()` methods remain synchronous, with their wait **before** DXGI
Present and their error ordering preserved. Both paths share command recording.

A single owner uses the device and its swapchain: no concurrent Present,
resize or upload. `NativeDevice` must outlive the viewers. Uploads, readbacks,
resizes and device destruction keep their synchronizations. DXGI Present can
still block; the asynchronous call is therefore no guarantee of an immediate
return.

## Why resources remain valid

`NativeDevice` already owns three allocators and three submissions. `beginFrame`
waits only for the fence of the allocator it reuses, then resets it. It adds
no fourth piece of work while that slot is still occupied. The command list
may be reset while a previous execution is running, but the chosen allocator
must be free. This is the
[Microsoft Reset contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-reset).

Each frame uses a separate ExecuteCommandLists call on the same DIRECT queue.
Microsoft guarantees complete ordering between these calls; the shared D32
depth buffer therefore remains sequential, without multiplying its resources.
This reasoning does not hold for lists grouped in a single call or for several
GPU queues.
[ExecuteCommandLists reference](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists).

Before their first recorded use, submissions retain buffers, textures, depth,
backbuffer, PSO, root signature and heaps. SRVs remain immutable between
synchronous uploads; the per-batch sampler heap is retained. Non-shader-visible
RTV/DSV descriptors are copied during recording, which allows their CPU slots
to be reused. The handles themselves provide no retention.
[Non-shader-visible heaps](https://learn.microsoft.com/en-us/windows/win32/direct3d12/non-shader-visible-descriptor-heaps),
[heap lifetime](https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-descriptor-heaps).

The existing persistent fault and quarantine are preserved: no second
Close/Execute during a drain. Resources remain owned until a fence proven
complete or a confirmed device removal, including after the local owners have
gone away. Backbuffer collection precedes `beginFrame`; this assumes precisely
the absence of any concurrent Present/resize.

## CPU counters and GPU measurements

`NativeDevice::metrics()` provides a cumulative snapshot:

| Counters | Definition |
|---|---|
| `begin_frame_calls`, `submit_calls`, `present_calls`, `idle_wait_calls` | Call attempts, including those refused or without work. |
| `explicit_wait_calls` | Public `wait()` calls, even when the fence is already complete. |
| `frame_slot_wait_calls` | Checks of the reused slot, without assuming an OS wait. |
| `execute_calls`, `signal_calls` | Attempts at the GPU queue boundaries. |
| `fence_wait_calls` | Entries into the event-based fence operation. |
| `event_wait_calls`, `event_wait_timeouts`, `event_wait_failures` | Actual WaitForSingleObject calls and their failures. |
| `cpu_*_ns` | CPU durations of completed scopes, success or error; they overlap and do not add up. |

The context is owned through `shared_ptr`, also from the operations retained
by the quarantine; no callback targets a `NativeDevice*`. Its mutex protects
only the addition and copying of the counters, never the GPU/OS wait.
A snapshot is structurally consistent but does not stop operations in
progress: their entry counter may precede their exit duration. Reading it
always assumes a live, already initialized `NativeDevice` object. The Win32
error of a failed wait is captured before the instrumentation.

The witness's GPU timestamps bracket only four synthetic copies. Their
conversion uses that queue's frequency, without equating CPU durations with
GPU time. No full-frame GPU time is announced.
[Microsoft: timestamps](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing).

## Bounded validation

Private evidence: `<private-evidence>/reprise-2026-10-30/async-native-present-cpp/`,
script `validate.py`, `validation-final.json` and logs. x64 Release build with
MSVC 19.44, `/std:c++latest /W4 /WX`, source SHA-256 unchanged during validation.
The latest mode is a draft mode, not full C++26 conformance.

**11/11 CTests pass** on RX 6950 XT and WARP, including 48 new checks per
adapter and the 47 existing lifecycle checks. No game, commercial asset or
external process was opened; the witness's HWNDs remain hidden. The results
establish:

- Three async presentations per viewer: zero public `wait()` calls; three
  synchronous presentations: three calls preserved. Execute and Present are
  called once per presentation.
- A GPU gate held by the witness blocks three copies in flight. The fourth
  slot really waits; the snapshot remains readable during the wait, then the
  four copied values are exact. The final forced durations are 24.5002 ms on
  hardware and 23.9638 ms on WARP, including a deliberate pause. They measure
  no performance gain.
- Invalid inputs refused before Execute, synchronized resize, exact synchronous
  RGBA readback; existing depth and texture tests unchanged.
- Three controlled submissions, the third of which fails at Signal: resources
  and callbacks retained, exact partial drain, confirmed removal and no double
  Execute. The former Close/timeout cases and the 64-submission/4096-object
  budgets pass.

The D3D12 validation layer is not available in these fixtures: the
`validation_errors` field is `null`, which is not an attested zero errors.
Faults are injected at the real controller, without causing a hardware removal.

## Remaining limits

`MeshViewer::prepareSamplers` still allocates vectors and one heap per batch.
`beginFrame` creates a submission and its state; its retention list may grow.
Shaders/PSOs are created at construction, files are read at load time; the
change moves no I/O onto the Present path. Depth is allocated on first use or
size change. A ResizeBuffers failure after the backbuffers are released keeps
its earlier recovery limit: the presentation path must be stopped and the
error handled, not continued as if the resize had succeeded.

Both CLI loops now use `present_async`, then a final drain before closing.
**50/50 integrated x64 CTests pass** (12.07 s). Two bounded interactive trials,
with the only source-owned synthetic bank, each present 163 frames in one
second; the bank remains unchanged. These counts confirm the wiring, without
constituting an FPS gain or a measure of regularity. The parent batch
`native-input-async-integration-v1/` keeps the commands, reports and hashes.

Full CPU/GPU frametimes, compositor latency, VRR, display changes and
streaming remain to be measured separately. This evidence defines no global
threshold of acceptable stutter. An independent review finds no new P1/P2
issue under the single-owner contract; it runs no additional tests.

Review points: [MeshViewer: commands and samplers](../../src/render/d3d12/mesh_viewer.cpp#L352),
[MeshViewer: presentation](../../src/render/d3d12/mesh_viewer.cpp#L799),
[TextureViewer: presentation](../../src/render/d3d12/texture_viewer.cpp#L344),
[NativeDevice: slots and waits](../../src/render/d3d12/native_device.cpp#L615),
[owned telemetry](../../src/render/d3d12/native_device.cpp#L17),
[reuse witness](../../tests/render/d3d12_async_present_test.cpp#L55).
