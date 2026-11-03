# Startup service initialization

The reference is the official PC MC 1.66 A executable, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
These independent C++ implementations use owned modern state and borrowed
allocation views. They neither execute retail addresses nor reproduce its ABI.

## Coverage

The candidate service tables were read from the immutable PE file. Their runtime
identity and purpose are not established. The following entry graphs reach their
returns within the observed window; their callees can still be unresolved.

| Entry | Controller index | Reconstructed behavior |
| --- | --- | --- |
| `98F020` | 0 | Object aliases, owner, constants and four ordered calls |
| `9CFA40` | 2 | Wrapped DWORD size calculation, allocation, final call |
| `982C40` | 3 | Owner and flag, two floats set to 1, three floats cleared |
| `998DD0` | 6 | Returns true without changing owner or flag |
| `9A7E10` | 8 | Conditional allocation setup, two calls and status handling |
| `999E80` | 11 | Query size, optional allocation, final call on both paths |
| `9B3F80` | 12 | Owner assignment and flag clear |
| `9B40C0`, `9B4060` | Helpers | Allocation and bounded BD/AD initialization fill |

`980AF0` now has a closed graph but no complete C++ driver here. The separate
[`9B1DF0` caller driver](startup_9b1df0.md) reuses its reconstructed tail while
retaining explicit interfaces for its callees. Services at indices 1, 4, 5 and
10 have no file-backed table established here. Other lifecycle
callbacks remain pure virtual: no empty successful implementation stands in for
a reconstructed service. The controller integration test uses a test witness.

## State and callback ordering

DWORD arithmetic wraps modulo 2^32; pointers and opaque forwarded values remain
x64-capable. Native addresses stored as data use `NativeIdentity` and cannot be
called through this API. Port names identify unresolved callees without assigning
an invented subsystem role. EAX is recorded at the two `41EE80` calls; whether the
callee consumes it is still unknown. The indirect calls through `9EC47C..9EC484`
are not identified PE imports and retain neutral names.

Each helper tests the first context's byte at +E4 and requests its DWORD size at
+EC. After allocation it reloads the global context, stores the allocation view,
then reads the reloaded context's +EC fill length. It does not retest the flag or
free an earlier block. The allocator/context must retain the storage for all uses;
assigning a borrowed view does not destroy its previous allocation.

A zero length performs no memory access. Missing contexts and a nonzero fill
outside valid storage produce explicit modern refusals. The store already made
before the second context or capacity check is preserved. A missing `143F520`
object in `98F020` is refused before effects instead of reproducing a native fault.
These refusals concern invalid native states, not authentication or connectivity.

## Evidence and validation

Rea Listing observations from `bootstrap-services-v1` supply seven complete and
two partial entry graphs. Helper assembly evidence identifiers are
`ev_d89c30ebb33ba30bdda07c18bb6750ad5686edb3b9144139d67f436e2bbdface`
and `ev_69c84d5e5d48147e51c7d2ec91fea8a3c5e0b90fa72fdffa9bda76364ec08317`.
The verified file constant at `F4165C` is float 1.0. Raw evidence stays local.

Run `startup_services` and `game_mode_lifecycle` with CTest in the x64 build.
Cases cover callback ordering, changed globals, full-width selectors, null
allocations, wrapped sizes, bounded fills and opaque values above 32 bits.
No gameplay, new-game loading, world simulation or co-op success follows from
these component tests. No retail game was started for this integration.
