# Native input command selection

The official PC MC 1.66 A reference has SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Rea assembly evidence
`ev_89ecfc1abd159a306a4593d9f1337c230ea90e7ca09765a6ea65e1f3bc882b5c`
establishes all 275 bytes and three returns of `980550`.

The helper selects description ID 1, ordinal 0 via `433DF0`, stores it, then
resolves `LEFT`, `UP`, `WHEEL_UP`, `WHEEL_DOWN`, `BUTTON_0`, `BUTTON_2`, `BUTTON_1`
in that order. The seven labels were independently checked with bounded
immutable reads of the verified file. Their names suggest mouse-related
descriptions, but do not establish the full startup service's role.

The C++ implementation reuses `find_native_input_slot`, the existing `433DF0`
reconstruction. Only the signed name lookup result from `434190` remains an
explicit interface. Every negative index clears its output. A missing description
clears all seven outputs. The native description identity stays fixed across
the callbacks; its record array is read again after each callback, as in the
listing. Already selected records retain their own lifetime when arrays change.

Owned modern descriptions and established `NativeInputRecord` data replace raw
32-bit addresses. Registry view mismatches, missing owned records and invalid
nonnegative indices produce modern refusals. They preserve effects already
completed and do not fabricate an out-of-range native pointer. These are defined
policies for states not representable by the valid modern model.

Run CTests `native_input_selection` and `native_input_state`. Scenarios cover
registry sentinels, ID zero, full-width IDs, index zero and negative indices,
lookup order, changed arrays, pinned description identity and partial effects.
No SDL transport, device enumeration, Xbox/PlayStation mapping, map navigation
or retail input dispatch is established by this helper. It is a component of
the independent client, not an installed controller fix for the original game.
