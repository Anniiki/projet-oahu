# Text initialization in the VMF player

## Result and evidence

The explicit backend `create_with_text_bindings` now performs the PC2.8 text
binding during the real placement of the frame player. `GENERAL` passes its
former stop on resource 37: 11 text resources are initialized and 14 variables
exist before a host stop on opcode `0x23`. The diagnostic draws no menu and
does not acknowledge that command.

Secondary static reference PC Paradise, SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
Private sources under `reprise-2026-10-31/rea/`:

- `vmf-display-lifecycle/session/49`: initializer `44BDC0`;
  `30`, `4483B0`: removal without unregistration if `resource+2E & 10`;
  `67`, `44A440`: real unregistration, kept as a boundary.
- `vmf-host-context/session-v2/18`, `446B70`: resource assignment, name
  clearing, text initialization, then destruction of the former child.
- `vmf-runtime-scheduler/session/07`, `4436A0`: typed copy, NUL on miss;
  `vmf-text-services/session/12/18`: classification and write `449240`.
- Batch `reprise-2026-10-30/rea/vmf-action-interpreter/session/108/96`:
  `4435D0` calls the hash `443390`, which dereferences the name without a
  NULL guard.

The manifests of the earlier batches cross-check these bodies against the PE
and DUMPBIN. This new wiring is neither an invocation of the official client
nor evidence of the equivalence of the Xbox code.

## Order and ownership

Resolution sees the new characterID, the cleared node name and the cache
child already attached. A bounded temporary state restores the few touched
links on error; no general cloning of the pools is performed.

The binding uses the current owner. A simple name does not search the parent.
An absent target keeps the scratch. An absent variable first writes NUL, then
tries the initial text, truncated to 31 bytes; this value is not immediately
copied back into the scratch. A full dictionary ignores the insertion but
still arms flag 200 of the resolved target. An empty key remains valid. After
a successful resolution, a NULL name is refused before the dangerous native
access; this is neither an empty key nor an ordinary miss.

The write and the ordered scope clearings are published together, including
when the target has just been armed and destroyed by the same placement. The
historical scope 0 rule is preserved. Without clearing, the setter follows
its usual path without copying the whole table.

Font/companion caches and two scratch buffers belong to the context. The
program and its immutable strings may be shared. Only the validated buffer is
published. The 64 KiB capacity and the initially empty scratch are explicit
modern choices, not capacities deduced from the binary.

## Usage, validation and limits

Build the program with `VmfTextLimits{}`, then enable
`VmfFramePlayerLimits::text_bindings`. This option also enables the dictionary.
The diagnostic offers:

```powershell
.\build\Release\tdur_inspect_vmf_playback.exe movie.vmf pc --text-bindings
```

The former flags remain independent. Without this backend, the earlier
boundaries remain active. Only PC texts with bit 2E&10 are covered; fields
requiring input/focus and the Xbox donor execution are refused. Removing a
covered text frees its node without clearing its owner scope.

The 17 targeted suites pass on x64 and Win32. The new test checks 303
conditions, 35 operations without ordinary allocation and 13 creation
allocation failures per architecture: bound values, caches, names, scopes,
late error, destruction order, rollback and GetVariable reads by the player.
The first fixtures without a TXIX table were corrected without modifying the
parser; the frame test also revealed the former removal guard, corrected
after rereading `4483B0`.

The 390 diagnostics on 39 private movies are identical across architectures.
The 312 earlier configurations remain unchanged. Of the 19 PC movies with
active binding, 13 reach `0x83`, two `0x23`, one `0x9F` and three complete
the 16 bounded invocations of the diagnostic. In total, 47 text resources are
initialized; 747 variables are present at the observed stops. These counters
do not measure game coverage. The 20 Xbox donor movies are refused at the
creation of this PC backend. Data and the two commercial executables keep
their hashes. None of these executables is launched.

Real input/focus, glyph layout and rendering, the remaining host commands and
the login path remain. No displayed commercial menu, chat or co-op trial is
validated by this batch.

The separate [`--properties`](vmf_properties.md) option now passes the `23`
stops of `GENERAL` and `ice`. The results above remain those of the text-only
mode, whose contract is preserved.
