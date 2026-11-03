# Foreground cursor policy for the official PC display bridge

This is an application policy in the Win32 observation mod, written in modern
C++. It is not the final x64 client or a native D3D12 renderer.

## Observed failure

The official MC1.66A executable is identified by SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The isolated B trial on 2026-11-02 recorded 2,502 `ShowCursor(FALSE)` calls
and two show calls before deactivation. Hides continued afterwards on the
same HWND thread, with return RVA `0x0059F87B` in the main image. The player
reported an invisible desktop pointer after Alt+Tab.

`ShowCursor` maintains a counter, rather than assigning a Boolean visibility
state. A negative counter hides the cursor. Repeating an unbalanced hide
every frame can therefore require thousands of matching show operations.
[Microsoft: ShowCursor](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-showcursor).

## Integration and behavior

`TdurInstallCursorDiagnostics` shares one user32 transaction between optional
cursor diagnostics and the independent foreground policy. The latter is enabled
by the requested display bridge (`TDUR_D3D12_DISPLAY=1`). Display device
creation registers the actual HWND; its subclass forwards unchanged native
activation messages, with policy callbacks before and after the native handler.

- The repeated hide shortcut applies only to the observed return RVA and an
  image verified by the owned launcher. Once the real owner-thread counter is
  negative, another hide from that site returns that observed value without
  decrementing again. Other active callers retain their normal API behavior.
- While the registered HWND and its children lack foreground ownership, the
  owner thread cannot warp, confine or recapture the desktop pointer. These
  operations return `ERROR_ACCESS_DENIED`. Background show/hide requests
  return terminal values for conventional visibility loops without modifying
  the desktop counter; background cursor-image changes are ignored.
- Deactivation releases capture only for the registered HWND, and clipping
  only if the current rectangle still matches the one this thread installed.
  Visibility adjustments are limited to 32. They are undone before native
  reactivation, with no accumulated background hide debt.
- No pointer coordinates are restored, no input is injected, and no focus,
  pause flag or window activation is fabricated. Cursor-policy logs contain
  operation names only and have a fixed cap for denied operations.

The release policy follows the public ownership contracts for
[ClipCursor](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-clipcursor)
and [SetCapture](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcapture).

## Validation and limits

`tdur_foreground_cursor_test` injects all desktop operations. Its regression
replays the 2,502 requests and verifies one effective hide, immediate release,
balanced reactivation, duplicate messages, foreign HWNDs/threads, changed clip
ownership, and bounded recovery from an already broken counter. It does not
move the user's pointer. The actual observation proxy must also compile.

The policy currently owns one display HWND and its owner thread. Other threads
keep their cursor counters. Hardware D3D9 cursor methods are not intercepted.
An unrelated caller with an already deeply negative counter can hit the
32-adjustment limit, which is logged rather than producing an unbounded loop.
The change has not yet been validated in a new player session. It does not
establish that the Oahu profile starts, or that controller/menu parity is ready.
