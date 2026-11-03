# Connected profile-command layer

The C++ x64 [command layer](../../include/tdur/game/profile_menu_dispatch.hpp)
connects the reconstructed profile core to three branches of official router
`7C5DE0`: `pnew`, `psel` and `pdel`. Reference: PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.

## Shared pending state

Let `T` be the router's saved context. The primary listings show:

- `pnew` passes `T + 27D3C` to `7C3CB0`; `psel` passes it to `7C36F0`.
- Those routines use pending at menu offset `150`.
- Both then pass `T - 1C` to gate `7C5A70`, which reads offset `27EA8`.
- `27D3C + 150 = -1C + 27EA8 = 27E8C`: the pending byte is the same storage.

`ProfileMenuScene` therefore owns one live menu and scene state. Selection
evaluates pending through the existing core before testing the gate. Creation
uses whatever pending value its injected creation service leaves, including
zero. A nonzero value writes scene state 4 before the mode-4 callback, which
receives that same live scene. Pending is not cleared. Removal only requests
the name operation; it never invokes creation or continuation.

The byte-tag adapter recognizes the first four bytes, preserving the native
prefix behavior for these three ASCII commands. Other commands and the earlier
`6C81F0` interception are outside this reconstruction. The pointer derivation
assumes that unresolved native callee removes its two arguments and preserves
EBX; it is not a reconstruction of that callee.

## Validation and limits

Released sources and inputs were hash-verified, the alias arithmetic was
rechecked, and the integrated canonical target was compiled independently with
MSVC x64 C++ latest and `/W4 /WX`. The dispatch test passes 100 checks for live
state, callback order, identity halves, creation failure and removal isolation.
Two independent runs of the unchanged 83-check core suite also detected nine
mutants in isolated builds.

Creation, profile storage, the mode-4 action and world loading remain injected
services. No commercial game was run by these tests. This layer does not prove
a working new game, controller-driven full UI or multiplayer session.
