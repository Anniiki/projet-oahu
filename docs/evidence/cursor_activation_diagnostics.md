# Opt-in cursor and activation diagnostics

The observation proxy can record bounded cursor metadata to investigate the
missing Windows pointer reported after leaving the official game's menu.
The reference remains PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.

## Enablement and recorded data

Use `tools/observe_official_start.py --cursor-metadata` with the existing
`--d3d12-display` diagnostic configuration and its required installation,
copy and output arguments. This option is rejected in a play session.
The wrapper removes any inherited `TDUR_CURSOR_DIAGNOSTICS` value and sets it
to `1` only for an explicit request. The proxy attaches nothing when the
variable is absent; any present value other than exactly `1` refuses startup.

Four Win32 operations are classified: `ShowCursor`, `SetCursor`, `ClipCursor`
and `ReleaseCapture`. Each wrapper invokes the original API once and preserves
its result and post-call last error. `WM_ACTIVATEAPP` and `WM_ACTIVATE` provide
markers before the native window procedure receives the unchanged message.

Budgets are 16 individual operation records per activation period, 256 marker
records and 4,096 records overall. Ten counters summarize intervening calls;
their complete widest summary fits in 192 bytes, checked at compile time.
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

Attachment inside the commercial game, real Alt+Tab behaviour and the reported
cursor defect have not been verified by these tests. D3D9 hardware-cursor
methods are outside this diagnostic: absent Win32 records do not establish
that no cursor operation occurred. This component does not establish a working
new game or a native D3D12 renderer.
