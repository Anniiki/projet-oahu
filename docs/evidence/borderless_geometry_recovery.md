# Recover borderless geometry after a host resize

The display bridge previously set the visible window style and size when a
mode was selected. A host resize or style change afterward could leave that
window smaller than its monitor until another explicit mode change.

Before frame pacing, the backend now compares a borderless window's bounds
and decorations with its current monitor. It reapplies the borderless style
only on a mismatch. Stable frames do not call `SetWindowPos`, avoiding a stream
of resize messages. Windowed and exclusive modes retain their own behavior;
minimized windows are not restored by this check.

The existing style operation preserves visibility and uses `SWP_NOACTIVATE`.
It neither focuses nor shows a previously hidden window, and never makes it
topmost. The regular monitor refresh still selects the display and its cap.

## Native validation

An owned Win32 fixture resizes and decorates its hidden borderless window after
backend initialization. The previous backend fails the monitor-bounds check.
The updated backend restores the rectangle and undecorated style, preserves
visibility and foreground ownership, and produces no positioning messages over
ten subsequent stable frames. A windowed host resize remains untouched.

MSVC C++ latest with `/W4 /WX` builds the backend, hooks and observation DLL.
Three native tests pass: window/unfocused-host behavior, D3D9On12 resource
interop, and classic-hook Reset/COM identity. Formatting and diff checks pass.

This validates the component failure and its correction. The actual game's
Alt+Tab scenario still needs verification, and the separately reported hidden
desktop cursor is not fixed by this change. The bridge remains transitional
D3D9On12 infrastructure, not the final native D3D12 renderer.

## Constraint before application (C2)

Repairing at the next frame lets the host first apply a smaller size: the
game receives `WM_SIZE` and `WM_WINDOWPOSCHANGED` at that size, and the
window can stay small while no frame is presented. The bridge subclass now
calls `constrain_window_message` after the native handler for
`WM_WINDOWPOSCHANGING` and `WM_STYLECHANGING`. In borderless mode, a foreign
size, position or decoration is replaced by the borderless geometry before
Windows applies it. The constraint never calls `SetWindowPos`, never shows,
minimizes or activates a window.

Ignored without a line: the bridge's own windowed, exclusive, restoring and
monitor operations, another thread or mode, exclusive mode, a destroyed HWND,
a minimized window or one being minimized. A target equal to another
monitor's exact bounds is left to the shell and logged as a migration. The
style constraint also applies while minimized: removing a frame does not
restore a window.

Exemption level (review item D1). Each bridge operation (mode change, repair,
monitor move, restoration) records the depth of the host's handler where it
began; the subclass wraps `DefSubclassProc` in `DisplayHostHandlerScope`. A
message at the operation's own level is the bridge's. A deeper message comes
from a call made by the host's handler meanwhile, typically the game's
resize callback answering the bridge's `WM_SIZE`: it is constrained when the
operation targets borderless (`bridge=reaction` in the line). At the
operation's own level, a borderless operation is not exempt either: its own
request equals the bounds, so any difference was written by the host's
handler into the message (`bridge=altered`). Outside any operation the line
reads `bridge=none`. Windows' own default processing inside the host's
handler counts as the host: during a windowed style change, `DefWindowProc`
answers `WM_STYLECHANGED` with a frame-only `SetWindowPos` (measured flags
`0x1001837`), which is neither constrained nor internal.

Abstentions are counted: when the window rectangle cannot be read or the
monitor bounds are empty, C2 cannot evaluate the request; the summaries count
it (`skipped=`) and `display-borderless-constraint-skipped reason=rect|monitor`
is written once per reason.

`TDUR_BORDERLESS_CONSTRAINT` absent enables the constraint, `0` disables it;
any other value refuses the bridge explicitly
(`display-borderless-constraint-invalid`). The accepted state is logged as
`display-borderless-constraint enabled=0|1 source=default|environment`.

Constraints and repairs share one guard. More than four corrections within
less than one second open a storm episode (one line); repairs are then
suspended for one second, while constraints stay applied. The end of an
episode empties the guard's ring, so under a cause that never stops a repair
is still applied after each suspension (four per episode): the host size is
kept at most one second at a time, never indefinitely (review item D2).

Lines are bounded but never stop for good: 16 individual
(`display-borderless-constrained`, `-repaired`, `-monitor-migration`) and 16
storm lines per backend, whose events stay counted; summaries
(`constrained repaired suspended storms migrations skipped`) only when a
counter changed, the first 32 at least five seconds apart, then at a doubling
interval (10 s, 20 s, 40 s...), announced once by
`display-borderless-correction-summary-sparse after=32
next_interval_ms=10000 doubling=1`; one final summary at destruction,
outside every budget. A ten-minute lasting cause yields 37 summaries and
the final one. They carry sizes, kinds and counters only.

Validation, on a transparent witness traversed by the mouse and never
activated (only `SW_SHOWMINNOACTIVE` and `SW_SHOWNOACTIVATE`): a foreign
320x240 resize and caption stay at the monitor bounds without any frame,
and no message reports the foreign size, while the same gestures without
the constraint are applied until the next frame repairs them; bridge mode
and monitor changes are exempt; a restore from minimization is constrained
at once (`restore_path=constraint`, `WS_MINIMIZE` not seen); the storm
guard suspends repairs after four and resumes after 1.1 s; the pure guard
and switch are exact; the real subclass constrains the bridged HWND; an
injected start with the variable at `0` and an invalid value logs the
expected state or refusal. Corrections of the review: a host that
redemands 320x240 from `WM_SIZE` keeps the bounds during
`set_mode(borderless)` and during a repair (`bridge=reaction`), with one
repair and no loop over eight frames; a host that rewrites the bridge's own
borderless request is restored (`bridge=altered`) while a windowed target
stays exempt; past 16 corrections one backend writes exactly 16 individual
lines and its final summary carries every counter; the real subclass also
constrains the fixture's reaction during `SetDisplayDeviceMode9`, checked by
`display_classic_hooks_wiring`, which stops before the second native Reset
that fails on the test machine. Not done here: `WS_MAXIMIZE` cleared by the
style operation, and the reaction of the game's own resize callback,
which no longer receives the small size; the actual game still needs a trial.
