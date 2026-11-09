# Sector bank resource reset and loading

`startup_sector_resource.hpp` reconstructs the callers `705CB0` and `706030`.
`SectorResourceBinding` connects them to the existing `705190` world loader.
Their remaining callees are required ports, not successful defaults. The
records are owned C++ state, not a native object layout or executable image.

## Primary evidence

Reference: official PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Existing Ghidra exports and bounded disassembly cover `705CB0..705E3B`
(396 bytes including alignment) and `706030..70616C` (317 bytes including
alignment). All 713 bytes match the unchanged official image. Additional
windows verify bank format `F3B084`, source-file identity `F3B154`
(`.\Sources\Sector.cpp`) and formatter slot `9EC378` (`B433AE`). No original
instruction or game was executed and no new analysis session was opened.

## Shared slot and payload fields

The sector slot contains its payload at native offset `+10`. Two fields are
therefore views of the same storage:

| Slot view | Payload storage | Meaning established here |
| --- | --- | --- |
| `+2B4` | `+2A4` | Status written by reset, load and invalid-coordinate paths |
| `+10E6C` | `+10E5C` | Property copied by `705190`, searched and cleared by reset |

Only the payload owns these values. Slot accessors return references to them;
there are no reference data members that could retain the source object's
address after a copy or move. The supplied buffer at **payload `+2B4`** is a
different field from **slot `+2B4`**.

## Reset order and callback-visible state

`705CB0` receives a payload stack argument. It first reads `10E7FA0` and, when
nonnull, calls `6FA640` with that context in EAX and the payload in EBX. It then
walks the list at payload `+10E58`, capturing each node's `+0C` successor before
calling virtual slot `+08`. The owned implementation pins the current and next
nodes during this call. Removal of their external references is supported;
destruction of the current node occurs after its callback returns. The body
does not itself clear the list head. An explicit host work budget refuses
cycles or overlong traversal rather than inventing successful completion.

Next come two first-match searches of registry `13D3E10`: tags `726F7061` and
`70617274` at object `+08`. The matching ports are `8A5520` and `8A4F60`, with
EDX identifying the payload and the matched object as a stack argument. The
signed count at `13D3E38` is captured for each search, including its reload
after the first callback. A callback may replace the registry and its count.
Missing host entries cause refusal only when visited. Nonpositive counts
perform no lookup.

If payload `+2AC` is nonnull, native reads of the linked area's `+CB4` and a
dead `+7D0` calculation have no call or write effect. The payload link and
byte `+2B0` are then cleared. A null link leaves that byte unchanged. The
native address-wrap guard is outside valid owned-object storage.

For nonzero ready byte `+2C`, the property at `+10E5C` is searched among the
seven records starting at `120BE0C`, stride `14` hexadecimal. Only the first
match calls `7D7A60` with EAX identifying the payload; the port also exposes
the caller's preserved ESI value, index times five. On return, byte `+10` and
DWORDs `+00/+04/+08/+0C` of that record are cleared. Its three other bytes
remain unchanged. Property `+10E5C` is cleared before `901AB0(FA4CF8, payload)`;
that notification also runs when the property was zero or no record matched.
The whole branch is skipped when the ready byte is zero.

The live `FA48B0` slot is read after these callbacks. Required calls follow:
`6FC800` with EAX identifying its `+20` member and EBX the payload; `6FF980`
with EAX identifying payload `+7D0`; `701FE0` with EDI identifying `+2C0`.
Finally, the body re-reads byte `+34`, buffer `+04` and allocator `+38`. Free
`674E0C(buffer)` runs only when the byte is zero and both pointers are nonnull.
Unlike area reset `709200`, this call site does not load the allocator into
ECX. The sector port does not invent that register argument. Buffer `+04`,
ready byte `+2C` and status `+2A4` are cleared on the returned path, even if
the free callback replaced the buffer. Other fields remain as last written.

## Load request and return paths

`706030` receives path in ECX, name in EAX, payload in ESI and six stack
arguments: area X/Y, sector X/Y, scale bits, data identity. It copies the
name forward, including its NUL, to `+10E00` before reading the selector or
formatting the bank path. The host reserves `58` hexadecimal bytes up to the
next known field; this is a conservative host boundary, not a claim about
the native array's declared size. Overflow refuses with the copied prefix
preserved. Bytes after the first NUL are not cleared.

The selector and format use the same identities as the
[area resource loader](startup_area_resource.md): nonzero selector chooses
`11404C4`, zero chooses `1140444`, level is `11405C4`, bank format is
`%sLevel\%s\%s.bnk`, and destination capacity is 512 bytes. An unavailable
selector, interrupted formatter or missing terminator refuses after any
preceding name writes.

If the ready byte is zero, supplied buffer `+2B4` is copied to `+04`, byte
`+34` records its presence independently of capacity, allocator `+38` becomes
`FA9CB0`, and byte `+3C` becomes zero. The bank port `9A4D40` receives service
`10E80C0`, the same resource's buffer and size fields, callback `9A6D50`,
capacity `480000`, source `F3B154`, line `36`, final argument one and EAX zero.

Nonzero native AL, or an already nonzero ready byte, stores data identity
plus `4F80` at `+2330` **before** calling `701340` on member `+2C0` with
`(area_x, area_y, sector_x, sector_y, scale_bits, 0)`, then writes status two.
Native AL=0 instead clears `+2330` and writes status three. Both native paths
store the data identity at `+2A8` and sector X/Y at `+29C/+2A0`. Neither sets
the ready byte. A modern port refusal stops before subsequent effects, and
is distinct from ordinary native AL=0.

Thus native rejection still returns to `705190`, which writes the slot's
property and coordinates. That normal caller return does not establish a
loaded resource or playable world.

## Validation and remaining boundaries

Tests cover alias writes in both directions, address identity, copy/move
independence, exact reset order, mutable registries, node unlinking and
lifetime, seven possible first matches, preserved padding, live global and
buffer changes, all free predicates, ready/AL/supplied-buffer combinations,
name/path bounds, captured arguments, every refusal stage and composition
with the actual `705190` caller. Previous area, world, preparation, cycle and
mode tests remain in the six-program validation.

Build `startup_sector_resource_test`; run CTest with
`-R '^(startup_sector_resource|startup_area_resource|startup_world_loaders|startup_service_preparations|startup_service_cycles|game_mode_lifecycle)$'`.
Validation uses x64 Release, `/W4 /WX`, one build job, BelowNormal priority,
the shared build mutex and a 6 GiB child guard.

Required leaf bodies remain `6FA640`, virtual release slot `+08`, `8A5520`,
`8A4F60`, `7D7A60`, `901AB0`, `6FC800`, `6FF980`, `701FE0`, `701340`,
`9A4D40` and memory ownership behind `674E0C`, plus the shared selector/path
sources and complete host installation. The [synchronization binding](startup_world_sync.md)
supplies `9A2A20`, `9A2BD0` and `9B4BA0`; its internal operations and `609A20`
remain required. Records and borrowed objects
must outlive their calls; concurrent unsynchronized mutation is unsupported.
No native pointer is converted to a host address. No loaded world, rendering,
playable client or two-client test is established by these tests.

Related: [world loading callers](startup_world_loaders.md),
[preparations](startup_service_preparations.md).
