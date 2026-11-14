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
  Visibility adjustments are limited to 4,096 real shows per loss. Before
  native reactivation a single hide is given back, not the whole loss
  compensation; the remaining compensation is given back only at unbind.
  There is no accumulated background hide debt.
- No pointer coordinates are restored, no input is injected, and no focus,
  pause flag or window activation is fabricated. Cursor-policy logs contain
  operation names and call counts only (`shows=N count=C` at release or cap,
  `hides=N count=C|none` at return and unbind, where `count` is the value the
  real API returned), and have a fixed cap for denied operations.

## Single hide on return

This is a defense of the cursor balance, not the correction of a measured
defect. A replay of the observed sequences of cycles 1 and 2 of a recorded
session (start-up, then return messages with two foreign hides and two
shows) keeps the counter at -1 in game with the previous rule and with the
new one: 162 real shows and 163 real hides over 40 Alt+Tab, cursor visible
after every loss, no cap. The replay does not prove the origin of the
foreign calls, and the placement of the two shows of each active period is
assumed (both placements are replayed).

In a constructed case, not observed in that session, a module hides twice
at each return without a compensating show. With the previous rule the
cursor stays hidden after a loss from cycle 17, the 32-show cap is hit 34
times in 50 cycles and the in-game counter drifts from -3 to -101. With the
new rule the in-game counter stays at -3, the cursor is visible after every
loss and the cap is never reached (148 shows, 151 hides). A throwaway
counter-proof compiled against the previous policy fails by assertion with
exactly those figures. Risk: if a module showed then hid at return, a
cursor could appear briefly over the game; the recorded session shows
hides first.

Second risk, found by the review and simulated, not observed: an Alt+Tab
that falls inside an active period where a module has hidden without having
given its shows back yet (recorded cycle 1: two foreign hides at
`WM_ACTIVATE`, two unattributed shows later in the period). The single hide
on return forgets that debt, so the deferred shows land on a shallower
counter: the simulation reaches +1 then 0, a Windows cursor visible over the
game until the second real hide of the game's site `0x59F87B`. The rule
never makes the counter deeper than the previous one (no cursor hidden by
mistake). To be checked in game by two close Alt+Tab right after a return;
the author of the two shows is measured by C5.

The release policy follows the public ownership contracts for
[ClipCursor](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-clipcursor)
and [SetCapture](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setcapture).

## Validation and limits

`tdur_foreground_cursor_test` injects all desktop operations. Its regression
replays the 2,502 requests and verifies one effective hide, immediate release,
a single hide on return, duplicate messages, foreign HWNDs/threads, changed
clip ownership, bounded recovery from an already broken counter (4,096
shows), the recorded-session replay and the unbalanced-module case. It does
not move the user's pointer. The actual observation proxy must also compile.

The policy currently owns one display HWND and its owner thread. Other threads
keep their cursor counters. Hardware D3D9 cursor methods are not intercepted.
An unrelated caller with an already deeply negative counter can hit the
4,096-show limit, which is logged rather than producing an unbounded loop.
The change has not yet been validated in a new player session. It does not
establish that the Oahu profile starts, or that controller/menu parity is ready.
