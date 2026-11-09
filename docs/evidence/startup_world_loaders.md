# Startup area loading, sector loading and drain

`startup_world_loaders.hpp` supplies the actual callers `708C80`, `705190`
and `9A4930`, plus area-slot lookup `706E30`. `LoadedServicePreparationPorts`
connects them to the existing preparation and service-cycle bodies. Required
leaf operations remain abstract; these tests do not establish a loaded world.

## Evidence and call contracts

Reference: official PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The existing Ghidra exports were read alongside bounded disassembly of the
unchanged official image. No original instruction was executed and no new
analysis session was opened.

| Window | Bytes | Purpose |
| --- | ---: | --- |
| `708C80..708D4C` | 205 | Area caller, register inputs and six stack arguments |
| `705190..7053D4` | 581 | Sector caller, two format calls and live property lookup |
| `706E30..706E69` | 58 | First matching slot among three neighbors and center |
| `9A4930..9A496A` | 59 | Pending-work loop and two ordered final calls |
| `706030..70616C` | 317 | Confirm six stack arguments, EBP and RET 18 |
| `F3B108`, `F3B128`, `F3B16C` | 32, 32, 48 | Three NUL-terminated path formats |
| `9EC378` | 4 | On-disk word `B433AE`, RVA of retained `sprintf` hint/name data |

The local record retains nine byte windows, five atlas-file hashes and equal
before/after image hashes. Padding inside the `9A4930` and `706030` windows
is retained; window size is not a count of executed instructions.

`B433AE` is not an executable function address: its retained hint/name data
starts at VA `F433AE` and names `sprintf`. The active PE IAT directory does
not cover `9EC378`, so this static observation does not prove runtime slot
resolution. The explicit host formatter implements only the documented
formats; it does not call an address obtained from this on-disk word.

`708C80` receives the area parent in EAX, the coordinate pair through EDI,
and a slot and flags on the stack. It calls `709200` with ESI identifying
slot `+10` before validating the coordinates. A failed coordinate check
writes `-1` to slot `+CC0/+CC4` and returns normally. Reset effects survive.
Valid coordinates form `Area-x/Area-x-y/Area-x-y` with native backslashes.
The scale link `+376C`, coordinates and data `+3770` are read after formatting.
The call to `709590` has the path in EAX, payload in ESI, X offset in EBX,
and `(x, y, y_offset, 41C00000, data, flags)` on the stack. Coordinates are
read again after that call before the slot stores.

`705190` receives its parent, slot, area pair and sector pair on the stack.
It calls `705CB0(slot + 10)` first. Invalid coordinates set both stored
pairs to `(-1,-1)` and slot `+2B4` to three, without changing its property.
A negative area X branches before dereferencing the area link. The remaining
bounds use the linked area dimensions and sector dimensions, respectively.

The two formatted strings are:

- `Area-x/Area-x-y/Sector-x-y-sx-sy` (seven substitutions; four additional
  native variadic arguments are present but unused).
- `Sector-x-y-sx-sy` (four substitutions).

Both calls use one captured formatter function. Coordinates for the second
call are read after the first returns. The linked area object and its scale
are read after both calls. For scale `(height,width)`, the offsets are
`area_x = width*x*768`, `area_y = height*y*768`,
`sector_x = (width*x+sx)*768 + (sy odd ? 384 : 0)`, and
`sector_y = (height*y+sy)*768`. All operations wrap at 32 bits, including
callback-modified values. No second coordinate validation is inserted.

### The lookup result is not a loading argument

At `7052DA`, EDX is the slot argument, advanced by `10E80`: lookup `706E30`
uses the slot's **previous** area pair, not the new input pair. It compares
the three neighboring area slots, then the center, returning the first
match. The caller stores the result, advanced by `10`, at stack local `+20`.

That local is not passed to `706030`. The six pushes at `70531A..705327`
are `(area_x, area_y, sector_x, sector_y, 40400000, data_76604)` in callee
argument order. At `706037`, EBP loads the sixth argument (entry ESP +18);
the callee returns with `RET 18` at `70616A`. ECX is the path, EAX is the
short name, and ESI is the sector payload. The Ghidra pseudo-signature counts
the ECX argument separately, which makes the call look like seven arguments.
The implementation preserves the lookup but discards its unused return.

After `706030` returns, the caller reads the input pairs again, computes
`sx + (sy + (y*11+x)*8)*8`, then loads global `FA48B0` and the DWORD at its
`84090 + index*4`. The global may have changed during a callback. The selected
word is stored at slot `+10E6C`, followed by the four current coordinates.

## Drain ordering and modern refusal

`9A4930` queries `9A2A20` with ECX equal to the supplied context identity.
While AL is nonzero, it calls `9A2BD0` with that identity in EAX and queries
again. An empty optional denotes a modern refusal, distinct from native AL=0.
After the loop it calls `609A20(100AF5C, 0)`, then reads the current mode-owner
slot `10E7238` and calls `9B4BA0(owner, 0)`. A callback can replace that owner.

The host explicitly supplies a maximum pump count. The original loop has no
such limit: reaching it returns `refused_budget` with prior effects intact.
It never executes the final calls or reports completion on budget exhaustion.
The host must keep referenced objects alive and avoid concurrent mutation;
callback changes on the owning thread are supported.

## Ownership and limits

Area and sector records own separate payload identities and coordinate fields.
`ForwardCycleObject` owns its bounded sector-property table; the loader reads
the same global slot as the existing cycle. There is no duplicate global
snapshot. Scale and area links are explicit borrowed host relationships.
No integer retail address is converted to a host pointer. Default-initialized
records do not claim to reproduce any native constructor.

The formatter implementation handles only the three attested integer formats,
with bounded output and required NUL termination. Hosts install it explicitly;
no successful formatter or leaf implementation is silently supplied. The native
formatter lookup is a dispatch read, not an extra callback. Its output strings
are borrowed only for the duration of the receiving call. The reconstructed
preparations use separate coordinate inputs and output records; arbitrary
overlap between those fields in other callers remains unproven.

Missing required links, an unavailable formatter, unterminated output, a
missing live global or an out-of-range property index cause modern refusal.
All preceding effects remain. The property table represents an ordinary
bounded object, not native address wraparound outside that object. Refusal
does not invent the final coordinate stores, preparation guards or cycle
ready flag. A native invalid-coordinate return remains a normal return.

The [area resource binding](startup_area_resource.md) now supplies `709200`
and `709590`, with their internal operations still required. The
[sector resource binding](startup_sector_resource.md) supplies `705CB0` and
`706030` and owns the shared slot/payload status and property fields.
The [synchronization binding](startup_world_sync.md) supplies `9A2A20`,
`9A2BD0` and `9B4BA0`, with its gate, signal and worker operations still
required. The body of `609A20`, allocation release `674E0C` and complete host
installation remain required.

## Validation

Four focused CTest programs cover the new loaders, preparations, cycles and
mode lifecycle. The new suite checks every cell in a `4 x 3` area grid with
`8 x 8` sectors against an independent row-major property enumeration; fixed
vectors check path text, offsets, parity and wraparound. It checks reset-before-
validation, first-match lookup, all leaf refusal points, missing links, bounded
strings, unchanged final fields on refusal, and live callback replacement of
coordinates, formatter dispatch, property global and mode owner.

Composition uses the actual `Service9b3f80`, `9B4120`, both preparation bodies,
both loading callers and the drain, with explicit test witnesses only at the
remaining leaf boundaries. It confirms that changing `FA48B0` does not replace
the cycle's captured object, while the later property read uses the new one.
Build `startup_world_loaders_test` and run CTest with
`-R '^(startup_world_loaders|startup_service_preparations|startup_service_cycles|game_mode_lifecycle)$'`.
Validation uses x64 Release, `/W4 /WX`, one build job, BelowNormal priority,
the shared build mutex and the 6 GiB child guard. No game, rendering or
two-client test was performed.

Related: [preparations](startup_service_preparations.md),
[service cycles](startup_service_cycles.md).
