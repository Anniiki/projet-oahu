# Primary profile-menu observations

Reference: official PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Rea/Ghidra 12.2 inspected 23 connected profile/menu procedures in one read-only
session. The original executable hash remained unchanged after closure.

Instruction addresses and normalized listing text match the supplied secondary
catalogue for all 23 procedures. This is a listing comparison, not independent
byte equality or a semantic audit. The primary evidence, extents and comparison
manifest remain outside Git in `profile-menu-subsystem-v1`.

## Connected behavior

| Entry | Observed behavior |
| --- | --- |
| `7C3340` | Copy only ASCII letters and digits from the input name to its identifier; preserve case and stop at NUL. |
| `7C34C0` | Clear four input strings before testing cancellation conditions; one branch resets pending identity and calls service slots 0 and 40. |
| `7C3670` | Convert the selected one-based row to 0..7; call slot 40 with the resolved name only when its two-word identity differs from current. |
| `7C36F0` | Resolve the selected row and compare both identity halves; clear pending only for the current identity in mode 0. |
| `7C3790` | Populate up to eight profile rows through the profile service and VMF variables. |
| `7C3CB0` | Filter and validate a new name, then use profile-service slots and a record template before the persistence call. |
| `7C3E80` | Read three VMF inputs, refresh rows and conditionally invoke `7C35F0`. |
| `7C5A70` | A nonzero pending byte writes state 4 and invokes the mode-4 action; zero has no effects. |
| `7C5DE0` | Route `pnew` through `7C3CB0` then `7C5A70`, `psel` through `7C36F0` then `7C5A70`, and `ngam` through `7C5650` with action 0. |

The row conversion uses x86 DEC followed by signed tests. In particular,
`INT32_MIN` wraps to `INT32_MAX` before clamping to row 7. A portable rewrite
must avoid signed-overflow undefined behavior rather than directly subtracting
one from a signed 32-bit value.

## Limits

Static output at `6093F0` and `415C30` is not a usable implementation contract.
`7C3B10` and `7C50B0` establish prefixes ending in jumps to other regions, not
complete validators or handlers. Profile templates, service implementations,
filesystem formats and world loading remain unresolved. Service-slot roles
must remain explicit where their semantic names are unproven.

These observations do not establish the cause of the offline Oahu new-game
hang. Modern x64 reconstruction and owned-fixture validation are separate from
a successful profile-to-world transition in the game.
