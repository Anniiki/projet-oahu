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
