# Area bank resource reset and loading

`startup_area_resource.hpp` reconstructs the complete callers `709200` and
`709590`. `AreaResourceBinding` supplies them to the established `708C80`
world loader. The resource is owned host state; it is not a native memory
layout, and no retail pointer is invoked or dereferenced.

## Primary evidence

Reference: official PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Existing Ghidra exports and bounded official disassembly establish both
bodies. All 364 bytes (`709200..70924B`, 76; `709590..7096AF`, 288) match
their listings. The retained record also checks `F3B084`, `F3B0F4` and
`9EC378`: bank format `%sLevel\%s\%s.bnk`, source identity
`.\Sources\Area.cpp`, and formatter identity `B433AE`. Before/after hashes
are equal. No original instruction or game was executed; no new analysis
session was opened.

## Reset order

`709200` receives the resource in ESI. It first calls `706210` with EAX
identifying member `+7C0`, then `701FE0` with EDI identifying `+2B0`.
After both calls return, it reads the resource's byte `+34`, buffer `+04`
and allocator `+38`. It calls `674E0C` only when byte `+34` is zero and
both pointers are nonzero; the allocator remains in ECX and the buffer is
the stack argument. Callback changes to these fields are therefore visible.

The final buffer `+04` clear is unconditional after a returned call, even
if the callback replaced that slot. The body also clears byte `+2C` and
DWORD `+CA4`. It preserves the size, allocator, ownership flag, supplied
buffer `+CA0`, position, data identity and unrelated fields. A modern refusal
from either reset or the free preserves prior callback effects and skips
all later operations and final clears.

## Bank request and common tail

`709590` receives the resource in ESI, relative path in EAX and X offset in
EBX. The six stack arguments are `(x, y, y_offset, scale_bits, data, flags)`.
The native EBP preserves the Y offset across callbacks. The modern body
copies these arguments as values before calling its required ports.

The selector byte `[[1140370]+E0]` chooses prefix `11404C4` when nonzero,
otherwise `1140444`. Formatting uses that identity, level string `11405C4`
and the incoming resource path with a 512-byte destination. The host must
supply the same selector/path sources as its other file operations; this
module installs no duplicate globals, filesystem paths or default provider.

After formatting, byte `+2C` controls the bank request. A nonzero byte skips
that request and proceeds to spatial initialization. Otherwise the supplied
buffer `+CA0` is copied into `+04`, byte `+34` records whether it is nonnull,
byte `+3C` becomes zero and allocator `+38` becomes `FA9CB0`.

The request to `9A4D40` preserves these arguments:

| Input | Value |
| --- | --- |
| EAX | zero |
| Service | `10E80C0` |
| Output buffer and size | Resource `+04` and `+30`, by reference |
| Callback and context | `9A6D50`, same resource object |
| Capacity | `180000` |
| Source file | `F3B0F4` |
| Source line / final argument | `2F` / one when flags' low byte is nonzero; `31` / zero otherwise |

Only the flags' **low byte** is tested (`7095D1`). A value such as `100`
therefore selects line `31` and final argument zero. Buffer presence is
independent of the bounded host allocation's capacity.

An ordinary native AL=0 skips `701340` and writes `+CA4 = 0`. Nonzero AL,
or the earlier ready-byte branch, calls `701340` on member `+2B0` with
`(x_offset, y_offset, x_offset, y_offset, scale_bits, 0)`, then writes
`+CA4 = 2`. A modern refusal is distinct from either native outcome and
does not fabricate the subsequent status or tail writes.

Both native routes finish by storing Y offset at `+2A8`, data at `+C9C`,
the position at `+29C/+2A0`, and X offset at `+2A4`. The ready byte is not
set here. In particular, native bank rejection still returns to `708C80`,
which stores its slot coordinates; that normal return proves neither a
loaded bank nor a playable world.

## Ownership, validation and remaining work

`BankLoadResource` represents the shared buffer and request fields. Area
payloads add separate owned identities for the two nested members, a borrowed
supplied allocation and the attested position/status fields. Host allocations
and linked objects must outlive their calls. The host also owns callback
behavior and resource lifetimes. Path views are valid only during the call;
an asynchronous implementation must copy what it retains.

Tests enumerate all combinations of the three reset/free predicates and
the ready byte, native AL, low-byte flags and supplied-buffer presence.
They check preserved fields, member identities, exact bank arguments,
post-callback reads, captured input values, all modern refusal points and
the native rejected route through the actual `708C80` binding. The previous
world-loader, preparation, cycle and mode suites remain in the five-program
validation. Build `startup_area_resource_test`; run CTest with
`-R '^(startup_area_resource|startup_world_loaders|startup_service_preparations|startup_service_cycles|game_mode_lifecycle)$'`.

Validation is x64 Release with `/W4 /WX`, one job, BelowNormal priority,
the shared build mutex and the 6 GiB child guard. `706210`, `701FE0`,
`701340`, `9A4D40`, selector/format sources and allocator ownership remain
required operations. The [sector resource binding](startup_sector_resource.md)
supplies sector reset/loading, also with required internal operations. Drain
internals remain subsequent work. No loaded world, game rendering or
multiplayer is established.

Related: [world loading callers](startup_world_loaders.md),
[preparations](startup_service_preparations.md).
