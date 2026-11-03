# D3D12 bridge to the official client

## Path actually implemented

This C++latest mod intercepts public D3D9 APIs and presents the result
through D3D9On12 and a DXGI flip swapchain. The commercial client remains the
official one; this path is not a complete recompilation of the engine. No
OpenTDU engine or Paradise DLL is loaded.

`TDUR_D3D12_DISPLAY=1`, supplied only to the private process by the wrapper,
arms the `Direct3DCreate9` gate on the already loaded module. DllMain loads no
runtime there, creates no device, thread or window and suspends no worker. The
first call to the factory initializes the complete hooks outside the loader
lock. The separate [continuity without activation](retail_background_policy.md)
option also arms a native renderer gate, after the launcher verifies and locks
the file. Its initial scope allows the three documented branch adaptations,
without waiting for an x86 stack walk.
The classic On12 factory preserves D3DPOOL_MANAGED textures. The real focus
HWND remains the game's; a hidden render sink separates D3D9 from the DXGI
presenter.

COM objects remain real. The hooks track their IUnknown identity and the
CreateDevice/Reset/Present methods, GetSwapChain and the primary swapchain's
Present. Release returns from an On12 tear-off do not become the death of the
main device. The methods of an interface returned by QI remain tracked; a
texture's GetDevice does not bypass the presenter.

The bridge default is borderless, a cap at the active monitor refresh rate,
VSync disabled and tearing only if allowed/supported. The requested mode is
chosen with `--display-mode borderless|windowed|exclusive`. Tearing support
describes VRR eligibility; no physical G-Sync/FreeSync activation is measured.
The preconditions and deferred exclusive mode remain documented in
[display_backend.md](display_backend.md).

The diagnostic option `--graphics-metadata` logs the descriptions of RT/depth
textures, RT surfaces and depth surfaces after creation. It forwards the
original parameters and HRESULT and retains no resource reference. The
presenter's internal calls are excluded. The log contains at most 64 distinct
tuples per device and a notice; counters and getters continue afterwards.
Repetitions of the native memory estimator are grouped. The
[detailed evidence](retail_render_targets.md) distinguishes attested
allocations, their unknown names and the visual usage still to be validated.

## Verified lifetime and errors

- Tracking nodes and thread handles are prepared before the Detours
  transaction. A C++ allocation after commit cannot leave a patched entry
  without a registered trampoline.
- A destroyed HWND does not mean the COM references are released: its context
  remains tracked until the device actually ends. Detachment refuses live
  objects. Destruction does not act on a reused HWND.
- An operation counter defers reentrant teardown. WM_NCDESTROY immediately
  marks the HWND unusable, without destroying the backend currently used by
  Reset or SetWindowPos. The test really closes its window from a synchronous
  message triggered by the mode change.
- Presentation remains forbidden after a failed Reset until the native Reset
  **and** the reattachment/tracking of the new swapchain both succeed.
- Every submission keeps the D3D9 surface, On12 interface, retired resource,
  sampled copy and D3D12/readback destinations. After a Signal, Return or wait
  error, the first HRESULT is kept and no new submission or reuse of an
  allocator or descriptor is permitted.
- `GetCompletedValue == UINT64_MAX` means device removal, never a successful
  frame. At shutdown, a new Signal can attest the completion of earlier work;
  the removal wait is bounded to 250 ms. If that completion remains unknown,
  the context and its resources remain retained until the process ends. No
  timeout is reported as GPU completion.

## Validation performed

MSVC 19.44.35229, Win32, SDK 10.0.26100.0, C++latest `/W4 /WX /permissive-`.
**15/15 CTest** of the mod pass, including six display scenarios:

| Test | Result |
|---|---|
| Window and host without focus | Modes/moves, restoration, 90 synthetic physics ticks and 60 logic ticks; no focus stolen. |
| Interop and pixels | Real On12/D3D12 devices, MSAA and MANAGED texture; exact synthetic pixels, unaligned formats/sizes. |
| Classic hooks | Normal factory calls, QI/GetDevice, swapchain, failed then successful Reset, mode changes and reentrant close. |
| Signal in error | Work really blocked on a fixture fence; single E_OUTOFMEMORY failure after Execute, refusal of subsequent calls, then GPU completion attested before release. |
| Removed device | RemoveDevice on the witness's only private object; HRESULT remembered and no new submission. No reset of the system adapter. |
| Injected startup | Real observation DLL, factory called by an EXE initializer before main; three Presents, On12 verified, hidden window without focus, 17 source canaries unchanged. |

The two error tests count 59 checks (37/22), with no skip. The D3D12 debug
layer is unavailable: no InfoQueue validation of lifetimes is claimed. The
fixture logs are kept in the mod's build.

## First official trial of the bridge

MC 1.66 A copy, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`,
trial `20261030T074026-34732`, PID 32276, 90 seconds. DLL used:
`53b081d36af8ee83d2979d199984e82b9f23ea96186b211530ddfa8ee7beabe1`.

The log attests several successful On12 creations and Resets, then a DXGI
presentation with HRESULT zero:

```text
on12=1 mode=1 source=1128x634 output=2560x1440 borderless=1
refresh=160/1 cap_millihz=160000 tearing=1 focus=0
```

The cap is configured at 160 Hz; this is not a measurement of 160 FPS nor of
physical VRR. The source surface remains 1128×634: internal resolution and the
maximum graphics preset are not yet ported. The null focus of this first
presentation does not validate world simulation in the background.

Private evidence: `<isolated-copies>/slot0/20261030T074026-34732/`.
Log 14 460 bytes, SHA-256
`00ea9e0f91d04a50be093f5e3f6aad114167066d65748ee8fe35abfbe76264d4`;
startup report SHA-256
`e41e11702b77dd9eb8a1b98e31b377bf7a3e6b7440a90daa2dfb1387b4e111a6`.
The Job stops only our process at the deadline, code 1460; slot released,
6 697 reference files and 17 source saves unchanged.

No game capture or profile manipulation is made: visual access remains denied
by the platform. The game's real pixels were not read by another route. The
finding here is that of the APIs and the presentation; visual appearance,
in-game progression, maximum settings, physics/NPCs and multiplayer remain to
be validated separately.

The logged state now distinguishes `exclusive` and `exclusive_pending`. A
hidden window initialized with the `exclusive` request can present while
waiting for its real activation; it is never reported as exclusive during
that wait. The injected fixture covers this startup, alongside the borderless
one. Mode changes also refuse a persistent GPU failure; they do not reuse the
resources of an unknown submission.

## Internal resolution: a useful negative trial

Trial `20261030T081533-16432`, PID 34584, tests for 90 seconds the enlargement
of only the public backbuffer parameters to the monitor. The log records a
successful presentation:

```text
source=2560x1440 viewport=1128x634 output=2560x1440 borderless=1
refresh=160/1 cap_millihz=160000 tearing=1 focus=0
```

The viewport kept by the game invalidates this approach as a port of its
native resolution. **This override is removed** from the bridge: the retained
behavior presents the surface actually requested by the engine. The fix must
set the internal configuration before the computation of views, projections
and interfaces, then verify those dimensions. No maximum setting is claimed.

Experimental DLL:
`0638f89a9db5d0f19cc3e13b624d70790a9168665b314dadba558e3311cda61c`;
log 14 114 bytes, SHA-256
`a417e67b9d16f040d96d0ca7d3edf79c4b33771e536f1708a0e484480fca9572`.
The probe reads 19/19 code ranges and the final audits confirm the sources
unchanged. This trial includes neither reading of game pixels nor a profile
click.
