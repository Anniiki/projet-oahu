# Connected profile-menu results

The x64 [result layer](../../include/tdur/game/profile_menu_result.hpp) shares
the owned `ProfileMenuScene` with the existing profile-command layer. Reference:
official PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.

## Observed contracts

Listener `7C47B0` has six contiguous decoded instructions ending in `RET 0C`.
Ghidra did not define a function there; the bounded Listing path establishes
these operations. The receiver is scene +20. It sets result-pending, stores the
first argument as result, then the third as context; the second is ignored.
These fields are distinct from the profile-selection pending byte.

`7C5A90` reads context and result before clearing result-pending. Contexts
0, 1 and 2 require result zero. Context 4 selects mode 1 or 2 for result zero,
or writes scene state zero otherwise. **Context 5 accepts every result**: its
referenced-tag service receives `(0, 1, 0)`, then state becomes 1 and the two
flags become 1. Unknown contexts only clear pending. Service calls receive
live state where the observed contract requires it; reentrant callback writes
are retained.

The consumer represents only `7C5BE7..7C5BFC`: a nonzero pending byte processes
one result and completes that segment. It does not recreate the rest of
`7C5B60`, its caller or update cadence. Native offsets describe evidence rather
than the layout of a packed C++ object.

## Verification and limits

The first version incorrectly rejected nonzero results in context 5. An
independent x64 witness failed before correction and passed unchanged against
the revision. The integrated MSVC C++ latest build passes 211 result checks
and the existing 100 dispatch checks. A separate mutation run detected 23 mutants.

The shared service `7C5570`, context-2 service `7C4A70`, referenced-tag service
and mode actions remain explicit ports. Their calling-convention assumptions
and unresolved effects are documented in the headers. The observed fixed
scene construction does not prove universal instance uniqueness. Unknown
callees beyond this layer may load resources; their effects remain unresolved.
No game was run by these tests, and no working new game or world loader is
established.
