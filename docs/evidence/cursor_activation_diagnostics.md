# Opt-in cursor and activation diagnostics

The observation proxy can record bounded cursor metadata to investigate the
missing Windows pointer reported after leaving the official game's menu.
The reference remains PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.

## Enablement and recorded data

This feature does not launch the game by itself. It is enabled through the
`TDUR_CURSOR_DIAGNOSTICS` environment variable, alongside the D3D12 display
diagnostic configuration. The proxy attaches nothing when the
variable is absent; any present value other than exactly `1` refuses startup.

Four Win32 operations are classified: `ShowCursor`, `SetCursor`, `ClipCursor`
and `ReleaseCapture`. Each wrapper invokes the original API once and preserves
its result and post-call last error. `WM_ACTIVATEAPP` and `WM_ACTIVATE` provide
markers before the native window procedure receives the unchanged message.

Budgets are 16 individual operation records per activation period, 256 marker
records and 4,096 records overall. Ten counters summarize intervening calls;
with the origin classifier two more (`ext1`, `ext0`: shows and hides from
neither the main image nor the retail site) are added. The complete widest
summary, 218 characters, fits in 256 bytes, checked at compile time.

## Caller origin (C5)

Each operation line ends with `origin=<class>`: `main`, `retail-site`,
`proxy`, `d3d9`, `d3d9on12`, `dxgi`, `d3d12`, `dinput8`, `user32`, `other` or
`unresolved`. Only this fixed class is written, never a module name or path.
The verified retail site (main-image RVA `0x0059F87B`) is recognized without
any API call; other callers go through a 16-entry cache, then
`GetModuleHandleExW` and the file-name leaf. The retail site is detailed at
most twice per period and then only counted, so it no longer hides the calls
of other modules within the 16-line window. Without a classifier the lines
are byte-identical to the previous format.

## Window operations and Reset parameters (C3)

With the same variable, the proxy also attaches `SetWindowPos`, `MoveWindow`,
`SetWindowLongA/W` (style indexes only), `SetWindowPlacement`, `ShowWindow`
and `ShowWindowAsync` in the same transaction, and records the operations
made on the HWND bound to the display bridge:

- `window-op` before each real call (requested size, flags, command or style
  classes, whether the rectangle equals a monitor, `origin=`), and
  `window-op-failed` with the Win32 error;
- `window-msg` for position, size, style, system-command, show and
  display-change messages, with `internal=` and
  `during=<operation>/<origin>` (intercepted call in progress on the thread);
  `internal=1` marks the bridge's own messages only, at the level where its
  operation began. A message produced by a call the game's handler makes
  while a bridge operation runs reads `internal=0`, as does a frame-only
  `SetWindowPos` that `DefWindowProc` makes inside that handler; the author
  of a call reads in `origin=` and `during=`;
  position messages report the request and the applied size, and whether the
  borderless constraint changed them;
- `display-present-request` for the presentation parameters the game asks
  for at each `CreateDevice` and `Reset`, before the bridge rewrites them;
- `window-summary` at the next activation marker.

Budgets: 32 lines per activation period, 64 presentation requests and 2,048
lines overall; later events are only counted. No coordinate, handle value or
window text is recorded, and no message or returned value is altered.
`window-diagnostics-installed` reports the shared transaction result.
Coordinates, rectangles, handle values, input events and window text are not
collected. The existing logger converts caller addresses to main-image RVAs.
The diagnostic never forces cursor visibility, focus, capture or pause state.

## Verification and limits

The integrated Win32 C++ latest build passes seven targeted CTests, including
45 injected-API checks, 19 launcher-option checks, observation confinement,
launcher lifecycle and display Reset identity. The option checks start no
process. A separate mutation run detected nine core mutants and six option mutants.

A further pair of integration tests verifies real Detours installation in the
source-owned observation witness and the default disabled path. Its ordinary
file-operation workload passes with the cursor transaction installed exactly
once. The test adds no cursor operation or system input. The fixture removes
the inherited diagnostic variable before choosing its explicit configuration.

The window diagnostics core is verified in memory by
`tdur_window_diagnostics_test` (exact formats, filtering, budgets, nested
attribution, no line without an instance); the display hook witness checks
the parameters recorded at a real Reset. The real origin classifier and the
window hooks are installed by the cursor fixture, but no test drives them
with a real caller.

Attachment inside the commercial game, real Alt+Tab behaviour and the reported
cursor defect have not been verified by these tests. D3D9 hardware-cursor
methods are outside this diagnostic: absent Win32 records do not establish
that no cursor operation occurred. This component does not establish a working
new game or a native D3D12 renderer.
