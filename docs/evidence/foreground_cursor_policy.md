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
  operation names, call counts (`shows=N count=C` at release or cap,
  `hides=N count=C|none` at return and unbind, where `count` is the value the
  real API returned), the trigger of each release and return and the other
  fields described in the pointer ownership section; denied operations have
  a fixed cap of lines.

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

## Pointer ownership beyond activation messages

This extends the same new policy; it does not reproduce a behavior of the
official executable. A player session reported an invisible Windows pointer
while the Xbox Game Bar (Win+G) or the Start menu was open over the game; the
pointer came back only after Alt+Tab to another window. That session logged
one release and one return per Alt+Tab and nothing else: the policy released
only on `WM_ACTIVATEAPP(FALSE)` or `WM_ACTIVATE(WA_INACTIVE)` received by the
bound window. A foreground lost without those messages only refused the
game's calls, and a window of another process in front of the game, the game
keeping the Win32 foreground, went unnoticed.

The game now owns the pointer only while the bound HWND or one of its
children has the Win32 foreground and the window under the pointer belongs
to the game's process. That window is the root (`GA_ROOT`) of
`WindowFromPoint` at `GetCursorPos`. It is foreign when it belongs to another
process, is visible and is not cloaked by DWM (`DWMWA_CLOAKED`). A read that
cannot decide (no position, no window or no process at that point) changes
nothing.

- Every intercepted call (`ShowCursor`, `SetCursor`, `ClipCursor`,
  `SetCursorPos`, `SetCapture`, `ReleaseCapture`) goes through one method of
  the policy, `intercept`: ownership is refreshed, then a refusable call is
  refused, and counted, while the game does not own the pointer. Lifting a
  clip and releasing the capture are never refused. An out-of-context
  `EVENT_SYSTEM_FOREGROUND` hook for all processes, installed on the HWND
  thread at bind and removed at unbind, refreshes ownership too; its
  callback arrives through the game's message loop.
- The window under the pointer is read at most once per 50 ms
  (`GetTickCount64`), and never while the foreground is elsewhere. The
  foreground callback reads at once; while an overlay holds the release, a
  moved pointer (`GetCursorPos` changed) is read at once; `WM_SETCURSOR`
  received by the bound window, the game in front, ends an overlay release
  without waiting for a read.
- A loss of ownership releases as a deactivation does: the game's capture
  and clipping are given back, the cursor is made visible with the stock
  arrow, and the game's calls stay refused while the other window is in
  front. Ownership regained takes the pointer back with a single hide, as on
  return. Nothing happens without a change of ownership. While a mouse
  button is held down (left, right or middle, `GetAsyncKeyState`), a foreign
  window starts no release: the game may hold the capture for a drag.
- While a deactivation message holds the release, polling does nothing and
  the activation messages keep their effect; a return message also forgets
  an overlay seen before. If the return message never comes, the foreground
  callback that finds the game in front again ends the deactivation in its
  place; a late message then has nothing left to give back.
- Each release and return names its trigger (`trigger=activation`,
  `foreground` or `overlay`). A release also writes `capture=0|1` (the bound
  window held the capture before it), `clip=0|1` (the game's clipping was
  lifted) and `showing=B/A`, the `CURSOR_SHOWING` state of `GetCursorInfo`
  before and after it (`-` when unreadable). A release by foreground or
  overlay names the window the pointer went to only when it belongs to a
  listed shell component (`GameBar.exe`, `GameBarFTServer.exe`,
  `StartMenuExperienceHost.exe`, `SearchHost.exe`, `ShellExperienceHost.exe`,
  `ShellHost.exe`, `explorer.exe`, `TextInputHost.exe`, `LockApp.exe`): its
  file name as `image=` and its window class as `class=`, in printable ASCII
  without spaces or `=` and at most 48 characters. Any other process gives
  `image=other class=other`, an unreadable one `unknown`, no foreground
  window `none`. Returns write the calls refused since bind (`blocked=N`).
  The first 64 releases and returns are written in full, then a single
  `foreground-cursor-transitions-capped` line; later ones are only counted.
  The unbind line writes `blocked=`, `releases=`, `returns=` and the reads by
  result (`read_game`, `read_foreign`, `read_cloaked`, `read_hidden`,
  `read_unreadable`); the bound line, whether the hook was installed
  (`event_hook=1|0`). The 128 lines of refused calls keep their cap. No
  coordinate, rectangle or handle is logged.

Reserve on the effect. Windows keeps the `ShowCursor` counter per thread: over
a window of another process the cursor follows that window's thread, unless
the game holds the capture or the input queues are attached. A release by
overlay therefore changes what the player sees only in those cases; it gives
back the capture and the clip in any case. Other explanations of the reported
symptom remain open: a capture held by the game, a DirectInput mouse in
exclusive mode (no path of this policy acts on it), or a Game Bar that is not
under the pointer or not returned by `WindowFromPoint`. The `capture`, `clip`
and `showing` fields of each release and the read counts at unbind are meant
to tell these apart; the cursor diagnostics should be enabled for that
session.

`WindowFromPoint` sends `WM_NCHITTEST` to the windows of the calling thread
(Windows behavior, not measured here). A read with the pointer over the game
window can therefore run the game's window procedure for that message in the
middle of a frame, from inside an intercepted call: at most once per 50 ms
while the game owns the pointer, and once when a moved pointer comes back
over the game during an overlay release. The window diagnostics do not log
that message.

Not established: the path by which the Game Bar and the Start menu come in
front of the game (a foreground change without activation messages, a
foreign window under the pointer, or neither). The session log points to the
second, the foreground watched by the gamepad service having changed only
during the Alt+Tab episodes, but it does not record when Win+G or Start were
pressed; a session log recording these key presses is needed to decide.
Not verified either: whether
`WindowFromPoint` skips cloaked windows, the order of the foreground callback
and the deactivation messages, and the capture and clipping the game holds
in front. Limits of the rule: an overlay that is not under the pointer
releases nothing, the cursor appearing once the pointer reaches it; a
click-through window is not returned by `WindowFromPoint` and is not seen; a
hit-testable foreign window over the game, even transparent, counts as in
front while the pointer is on it. The release acts only on the visibility
counter, capture, clipping and cursor image of the game's thread. Not
validated in a player session.

## Validation and limits

`tdur_foreground_cursor_test` injects all desktop operations. Its regression
replays the 2,502 requests and verifies one effective hide, immediate release,
a single hide on return, duplicate messages, foreign HWNDs/threads, changed
clip ownership, bounded recovery from an already broken counter (4,096
shows), the recorded-session replay and the unbalanced-module case. Its
pointer ownership cases cover a foreground lost without messages, a window
of another process under the pointer with the game in front (refused calls,
return once the pointer is back, 50 ms reads, early reads of a moved
pointer, return on `WM_SETCURSOR`), buttons held down, cloaked, invisible
and game process windows, unreadable reads, the foreground hook
(installation, callback, removal, end of a deactivation without return
message), an activation return with an overlay seen before, unrefusable
calls, the release fields, read counts and transition lines, the names
written and calls made during a release or an unbind. The recorded
sequences keep their figures with the hooks transcribed through
`intercept`. It does not move the user's pointer. The actual observation
proxy must also compile.

The policy currently owns one display HWND and its owner thread. Other threads
keep their cursor counters. Hardware D3D9 cursor methods are not intercepted.
An unrelated caller with an already deeply negative counter can hit the
4,096-show limit, which is logged rather than producing an unbounded loop.
The change has not yet been validated in a new player session. It does not
establish that the Oahu profile starts, or that controller/menu parity is ready.
