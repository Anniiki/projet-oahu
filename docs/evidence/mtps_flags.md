# MTPS object flags: 7D6B80 and 7E7A00

`MtpsFlagContext` carries the two memory helpers called during tutorial
transitions. The C++ structures have typed bindings; they do not reproduce
the x86 ABI and follow no process pointer. The full business role of the
objects signed `MTPS` is not inferred from that name.

## Reconstructed behavior

Each routine walks eight sub-objects, native offsets 10E90 apart. It rereads
the globals 10E7F6C/FA48B0 on every pass, then retains the selected
sub-object. The three reads of kind+2A4 remain distinct. The first two tests
may skip the pass; if the third value is no longer 1, the mask would produce
a dereferenced NULL base before JLE. The port then reports
`null_masked_bucket`.

The count +EEC4 is signed and reread after each entry. The inline base +EECC
stays retained; its slots are read as the walk proceeds. A NULL first slot,
a NULL type or a signature other than 5350544D skip the entry. An unknown
binding stays pending and is not treated as zero.

`selective(filter)` reads the byte buffer+A0 and the object identity +3D0,
including when that byte is 0. Null byte: set the bits. Otherwise, look up
the identity in eight DWORDs; set if found, clear otherwise.
`activation(argument, filter)` tests only the low byte of the argument.
If it is 0, clear without reading the global filter or the identity.
Otherwise, apply the same filter from 120BE98 and the eight IDs
120BE9C..120BEB8. The filters are reread for each eligible entry.

The writes preserve all other bits:

| Routine | First store | After rereading the slot |
| --- | --- | --- |
| 7D6B80 | bit 0 | bit 1 |
| 7E7A00 | bit 1 | bit 0 |

The second write uses the reread object, without repeating the type,
signature or filter guards. That NULL pointer would be dereferenced: modern
error `null_second_object`, with the first write preserved. Merging the two
stores into a single operation would alter this sequence.

## Usage and binding limits

```cpp
auto context = tdur::game::MtpsFlagContext::selective(filter);
auto result = context.run(inputs, 128);
```

The filter references are valid and fixed for the duration of the context.
The roots represent eight sub-objects whose derived native addresses are
non-null and non-wrapping; LEA tests on corrupted addresses are outside this
domain. Storages stay alive without relocation, with serialized access.
Fields may change between resumptions, but the base already selected, the
loaded values and the decisions taken are retained.

Budgets, unknown bindings and errors are modern boundaries. They do not
claim to reproduce a native pause or to guarantee its atomicity. A fault
forbids replay. A move transfers the in-progress effects and invalidates
the source. `flag_writes` counts completed stores, even when the bit already
had its value. No native boolean return is invented.

## Validation of October 31, 2026

MSVC Release C++latest, `/W4 /WX /permissive- /utf-8`, x64 and Win32:
seventeen targeted suites pass, including 1152 MTPS checks per architecture.
They cover guards, signed count and low byte, eight IDs, reread counts and
filters, slot replacement between stores, retained values, move and faults
after partial effects. Ordinary allocations instrumented during these runs
remain zero; this is not a global measurement of allocations.

```powershell
cmake --build build --config Release --target mtps_flags_test
.\build\Release\mtps_flags_test.exe
```

Rea, PE and DUMPBIN cross-check two complete bodies (572 bytes) against the
official primary on disk: 7D6B80/287B and 7E7A00/285B. The first AddressSet
is non-contiguous: 7D6B80..8C then 7D6B90..7D6CA1; the three alignment bytes
7D6B8D..8F are excluded. No targeted observation or execution in memory is
claimed for this batch. Private evidence:
`reprise-2026-10-31/mtps-flags-runtime-v1/`.

An earlier review read the four sources (43828B) and reported no concrete
defect; the hashes were then verified and the verdict bounded
to the documented domain. The erroneous wording of a previous review about
the JLE guard is not adopted. The upper callers and their notifications
remain to be connected. These test objects do not prove usable commercial
menus, a running world, a native D3D12 renderer or a functional co-op.
