# Win32, D3D9On12 and DXGI display bridge

## Scope and origin

`mod/src/display_backend.cpp` is an original C++ foundation built on the public
Windows interfaces. It contains no OpenTDU engine code, no decompiled shader and
no game offset. Its full-screen copy shader is written for this project.
The [inventoried OpenTDU work](../opentdu_display_inventory.md) is a list of
behaviors to port; this bridge does not mean that the game is recompiled.

The [Microsoft interoperability specification](https://microsoft.github.io/DirectX-Specs/d3d/TranslationLayerResourceInterop.html)
describes the On12 factories, retrieval of the D3D12 device and the resource
unwrap/return contract with fences. The Windows 10.0.26100 SDK provides
`d3d9on12.h`. The system DLL is loaded by absolute path, outside DllMain; no
game DLL and no custom version of the Microsoft component is loaded.

## Implemented behavior

- Default borderless on the pixels of the current monitor; windowed with frame,
  exclusive full screen through `IDXGISwapChain::SetFullscreenState`, then
  verification of the actual state. A hidden/inactive window is never declared
  exclusive. Refusals are explicit; transition errors restore the previous mode,
  styles and rectangle. Destruction restores the window.
- Native desktop resolution in borderless/exclusive; no arbitrary resolution,
  registry, global focus change or proprietary preference. Exclusive uses the
  monitor with the same LUID as the device; an output on another adapter is
  refused. No adapter index is persisted.
- Rational DisplayConfig refresh rate of the covered monitor, GDI as a reported
  fallback; ambiguous clones are flagged. Explicit migration and screen-change
  detection, refresh-rate revision every 500 ms and cap reset.
  Default `native`, choices `native_minus_three`, fixed or unlimited.
- QPC deadlines and high-resolution waitable timer when available, without a
  global `timeBeginPeriod`. Focus does not enter the cap computation.
- Classic `Direct3DCreate9On12` factory for D3D9 callers, and a distinct Ex
  factory for callers that choose it. No classic D3D9 fallback is presented as
  D3D12: QI `IDirect3DDevice9On12` and `GetD3D12Device` must succeed.
- D3D9 render window distinct from the flip-discard DXGI swapchain window.
  MSAA resolve by D3D9, On12 unwrap, D3D12 copy into a sampleable texture,
  copy shader to the backbuffer and On12 return with its fence. The return is
  in COMMON; each use/allocator/descriptor waits on its own fence.
  No D3D9 use is scheduled during the unwrap.

The [Microsoft VRR/DXGI contract](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/variable-refresh-rate-displays)
requires flip, tearing support, a zero interval and the appropriate flags. The
bridge keeps ALLOW_TEARING across ResizeBuffers and uses
DXGI_PRESENT_ALLOW_TEARING only in windowed/borderless without VSync. `vrr_eligible`
expresses this condition; it does not mean that the monitor enables FreeSync,
G-Sync or Adaptive-Sync. Physical verification remains necessary.

## Standalone evidence of October 30, 2026

Win32/x86 build with MSVC 19.44.35229 and SDK 10.0.26100:
`/std:c++latest /W4 /WX /permissive- /utf-8 /EHsc`, zero warnings.
This flag selects the compiler's latest mode, not a complete implementation
of a future C++ standard.

```powershell
cmake --build build-display-backend --config Release --parallel 3 --target tdur_display_backend_test
ctest --test-dir build-display-backend -C Release -V
```

| Scenario | Attested result |
| --- | --- |
| Mode/cap/tearing policy | All 16 exclusive/support/permission/VSync combinations are verified; fractional, invalid fixed and unlimited caps are covered. |
| Two real screens | DISPLAY1 and DISPLAY2, initially 160/1 Hz; rectangle migration, decorations, hidden state kept, no focus theft and initial restoration verified. |
| Host genuinely without focus | 61 phases of the witness's UI pump and network service, 90 physics ticks and 60 logic ticks over 1.00034 s; no abandoned debt. The network service is a scheduling counter, not retail traffic. |
| Hardware On12 | Real On12 QI, D3D12 device and LUID of the Radeon RX 6950 XT; tearing support=1 and maximum aniso=16. |
| Pixels/resources | Eight 70×38 images with four distinct quadrants, read back in full through D3D12; MSAA 1 then the maximum supported 8, four consecutive reuses per case. Pitch not a multiple of 256, channel order and orientation verified. |
| Classic D3D9/MANAGED | D3DPOOL_MANAGED texture locked/filled then used by a DrawPrimitiveUP; the 2 048 pixels of the 64×32 quad are exact after D3D12 readback. |
| Presentation | 13 successful DXGI calls, including a windowed/borderless round trip, after the first complete passes. No visible screen is required for this evidence. |

First complete passes: **2/2 CTest**, 222 window/host checks and
2 200 interop checks. Additional error checks are then added: hidden/inactive
exclusive refusal without rectangle/style change, null surface refused before
any barrier, then normal rendering reused.

A separate first visible attempt produced `0x887A0022`
(NOT_CURRENTLY_AVAILABLE), without validating exclusive. This HRESULT is also
used by our window guard; it is not enough to attribute the refusal to the
DXGI call. A documented repetition under
`<private-evidence>/reprise-2026-10-30/display-exclusive-current/result.json` lasts 0.231 s:
window visible=1, window/process foreground=0. The error explicitly names
`exclusive requires a visible foreground HWND`, so this repetition
**does not call SetFullscreenState(TRUE)**. It is not established that the GPU
refuses exclusive. **Real exclusive remains unvalidated.** The code does not
replace this result with a borderless window announced as exclusive.
The optional witness is explicitly named
“TDU reconstruction - témoin D3D9On12 (aucun jeu)” (TDU reconstruction,
D3D9On12 witness, no game):

```powershell
.\build-display-backend\Release\tdur_display_backend_test.exe --exclusive-visible
```

This visible scenario is not run by CTest. It requests focus only in the
witness and closes its window within two seconds in normal operation.
The backend never requests the foreground itself.

At startup, an exclusive request now stays pending if the HWND is
hidden/inactive: device creation and windowed presentation can succeed.
`exclusive_transition_pending=true` and `exclusive_active=false`
describe this state. A later presentation attempts the transition only
if the real window becomes visible and foreground; no fake activation
event is sent. The explicit `set_mode(exclusive)` call keeps its immediate
refusal when its preconditions are missing. Every mode change also checks
for the absence of a persistent GPU failure before touching the swapchain.

A second injected test actually requests this mode on the hidden witness: three
Present calls succeed, requested mode 2, exclusive 0, pending 1 and no focus
stolen. It validates the deferred startup, not an acquisition of hardware
exclusive.

A pass concurrent with the official trial observed DISPLAY1 at 50/1 Hz and
DISPLAY2 at 160/1; the Ex cases still passed, but classic CreateDevice was
refused. These concurrency results are kept as an availability limit of the
environment, distinct from the two complete passes.
Do not run the GPU witnesses during an exclusive retail trial.

## Explicit integration and limits

In `mod/CMakeLists.txt`, after the C++ settings and enable_testing:

```cmake
include("${CMAKE_CURRENT_SOURCE_DIR}/tests/display_backend_tests.cmake")
```

The `display_hooks_tests.cmake` and `display_failure_tests.cmake` snippets
complete this foundation. The proxy is now linked to the bridge, enabled
explicitly by the wrapper's `--d3d12-display`; diagnostics without this option
keep the original path. The factory gate is armed before the entrypoint, but the
real graphics loading/setup is deferred to the first public call, outside DllMain.
The methods of the real COM objects are intercepted: identity, GetDevice
and secondary interfaces are preserved. Reset and the primary swapchain's
presentations go through the same context. Partial presentation and
additional swapchains are refused explicitly.

The [hook report](display_hooks.md) describes the lifetime, the error
trials, the verified injection and the first trial on the official copy.
A DXGI_STATUS_OCCLUDED return remains an occlusion status, not a proven frame.

The fixture requests the highest supported MSAA capability and MaxAnisotropy.
This establishes no “maximum quality” preset of the original game: LOD, textures,
shadows, reflections, HDR and original preferences remain to be located and proven.
The bridge does not blindly transform the game's samplers or surfaces.
The sources currently accepted are resolved single-mip/single-slice RGB32
surfaces; HDR, multiple outputs, shared depth and additional formats
remain explicitly outside this sub-format.

DPI evidence comes from a PerMonitorV2 fixture; the DPI context and
interface coordinates of the real PE must be verified separately.
Physical screen disconnection, clones and multi-GPU outputs are not
validated on hardware. Copies, allocations and waits are synchronous:
this prototype claims no in-game performance. WARP and the D3D12 debug
layer are not covered by this On12 bench.

Finally, no focus loss is faked. The standalone C++ scheduler advances
without focus, but the original's pause, audio and DirectInput still have
to be identified at their public calls or proven routines. The bridge writes
no internal simulation, save, activation or protection state.
