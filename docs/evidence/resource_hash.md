# C++ calculation of 64-bit resource keys

## Reference and scope

`hash_resource_bytes` reconstructs `504980/117`, called by thunk
`504920/5`. Program selection uses this calculation through `602DD0`
on an **88-byte** variant descriptor; see
retail_shader_selection.md.

These bodies and their table are first established from the local
secondary executable, SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
Both bodies differ from official MC1.66A on disk, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Official trial 24 confirms both bodies in the
identified official process, equal to the secondary. It also confirms
all 256 table entries; code presence does not prove execution.

A single Rea session reuses the closed snapshot from the
program study. It is exported and closed after nine operations;
private batch `<private-evidence>/reprise-2026-10-30/rea/shader-variant-hash/`.
AddressSets are complete and contiguous; no original function or
game resource is executed for this reconstruction.

## Contract and word order

Native initialization is ESI `FAC432B1`, ECX `0CD5E44A`. A signed count
less than or equal to zero immediately returns scalar seed
`0CD5E44AFAC432B1` without reading input. Otherwise only the first
`count` bytes are processed as unsigned values.

The logical shift register places ESI in the high DWORD and ECX in
the low DWORD. For each byte, it shifts left eight bits, inserts
that byte at the bottom and applies the table entry selected by the
old high byte. Return EDX:EAX swaps the DWORDs back. There is no
final XOR. C++ uses unsigned 64-bit operations and swaps words only
at boundaries; it does not reuse binary registers or pointers.

`resource_hash_key_bytes` then serializes the result into eight
little-endian bytes, EAX then EDX, for key lookups. No name, terminal
NUL, text encoding or case is normalized.

The table at `F465D0` contains 256 DWORD pairs. With their roles
swapped, **all 256 entries** equal eight MSB reductions using polynomial
`0060034000F0D50B`. C++ generates this table at compile time; no
commercial blob is embedded. SHA-256 of the observed 2 048 bytes:
`56bd54524aff39e0c6908390292a97021c2a818bc3ab6fae34ffb72866f54b37`.
These bytes agree across both disk files and official observation
24: 256 bounded eight-byte scalar reads, all complete. They do not
form an atomic snapshot of the table.

The port rejects a positive prefix longer than the span, and the
full-span overload rejects a size not representable in `int32_t`.
These explicit guards are not claimed to be native checks.

## Independent validation

MSVC latest `/W4 /WX`, x64 and Win32: **26 370 checks without failure**.
The test author derives a bitwise oracle from assembly without
consulting the C++ implementation. It independently confirms every
table entry; a synthetic 4 096-byte vector reaches all 256 indices.

| Input | Scalar result |
| --- | --- |
| Empty or count ≤ 0 | `0CD5E44AFAC432B1` |
| One `00` byte | `86CB7EDEE5F3818C` |
| One `FF` byte | `86CB7E21E5F3818C` |
| ASCII `123456789` | `DCFB70C01B439313` |
| 88 zero bytes | `939323BCD3DB3625` |

Actual guard pages in the source-owned bench check no read for a
count ≤ 0, stopping at the exact prefix and rejection of short spans.
Serialization, offsets, canaries, 88-byte descriptors and inputs up
to 1 MiB are covered. No invalid span is fabricated to test `SIZE_MAX`:
the oversized-full-span guard is not claimed exercised. Private
evidence: `<private-evidence>/reprise-2026-10-30/resource-hash-cpp/`.

```powershell
cmake --build build --config Release --target resource_hash_test
ctest --test-dir build -C Release -R native_resource_key_hash --output-on-failure
```

Producers of all variant bytes, their mutations, the program registry
and corresponding shaders remain to be reconstructed. Multiplayer
challenge `97D830` remains separate, unresolved work.
