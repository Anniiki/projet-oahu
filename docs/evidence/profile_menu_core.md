# Reconstructed profile-menu core

The modern x64 C++ core in
[profile_menu_core.hpp](../../include/tdur/game/profile_menu_core.hpp) and
[profile_menu_core.cpp](../../src/game/profile_menu_core.cpp) reconstructs
connected profile-menu rules from official PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The [primary subsystem observations](profile_menu_primary.md) describe the
Rea provenance and unresolved services.

Implemented contracts:

- `7C3340`: filter a terminated name to case-preserving ASCII letters/digits.
- `7C3670`: the `pdel` route requests a name operation only for a noncurrent
  64-bit identity; it does not implement the underlying removal service.
- `7C36F0`: record the selected identity and its mode-dependent pending flag.
- `7C34C0`: clear inputs before the cancellation guards, then preserve the
  ordering of reset state and service calls on the admitted branch.
- `7C5A70`: a nonzero pending flag writes state 4 before the mode-4 action.

The rank conversion preserves x86 DEC wrapping, including `INT32_MIN` mapping
to index 7. Identity comparisons use both 32-bit halves. Owned text buffers
replace native pointers. The identifier's maximum inferred storage ends at
the observed flag at offset `148`, so its capacity is 64 bytes, including NUL.
Malformed unterminated input is explicitly refused without changing the
identifier; valid names are never silently truncated.

Unknown services remain injected ports. Unlike the native repeated getter,
this core assumes one stable provider during each operation. It does not
reconstruct a changing provider, profile file formats, serialization, online
login, template creation or world loading. The standalone gate remains
available; the [connected command layer](profile_menu_dispatch.md) binds its
pending byte to the live menu fields on the established `pnew`/`psel` paths.

## Validation

The revised proposal was hash-verified, copied exactly and independently
compiled in the canonical MSVC x64 build with C++ latest and `/W4 /WX`.
`profile_menu_core` passes 83 checks covering call order, high identity halves,
rank limits, refusal branches, malformed storage and a no-effect false gate.
The proposal's seven mutation cases were detected in its isolated build.

This is a reconstructed component for the independent client. It is not
injected into the commercial executable, does not resolve the Oahu new-game
hang, and does not establish a playable client or cooperative session.
