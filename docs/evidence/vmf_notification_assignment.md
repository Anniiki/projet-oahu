# Notification identity assignment: 894D50

`VmfNotificationAssignmentContext` carries the quota lookup, the insertion of
encrypted identities, the removal of matches and the increment of the final
counter. It reuses the real `hash_resource_name`, the XTEA primitives and
`clear_vmf_notification_identities`. The test catalogs and hosts are
synthetic data; the complete host service remains to be connected.

## Reference and evidence

Official PC MC 1.66 A executable, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The 434 bytes of the ranges `894D50..894DBC` and `894DC0..894F04` are compared
against the Rea exports of the secondary and against the primary on disk. The
three intermediate alignment bytes are excluded. SHA-256 of the concatenated
ranges:
`010922009a821145bfb97aabc68bb67b37ab07b5d9e4af8a5148adba6c9ac6ff`.
The private evidence is `vmf-identity-assignment-runtime-v1/native-proof.json`.
No new native or in-game execution is required for this port.

## Catalog and insertions

Each invocation captures the global catalog, its count, then its base.
The term `count * 56` and the bound are computed modulo 32 bits; the address
comparison is unsigned. The first exact match of the name hash provides the
DWORD at `+28`. Its functional name remains to be confirmed; it serves here as a
quota. Without a match, it is `FFFFFFFF`.

The context walks exactly 640 records. For a non-matching identity, it
decrypts the field twice, rereading its bytes: the second result determines
whether the slot is empty. The insertion modifies only the identity and uses a
signed comparison against the quota. If the available capacity is
insufficient, the return value is zero and the insertions remain in place.

A match calls the real removal `894CC0`, which clears every occurrence and its
associated fields, including the identities inserted earlier in this pass. The
recursive return is carried by a tail resumption that rereads the global
catalog. No C++ stack growth or arbitrary depth threshold is added. A `0/0`
request that meets an empty slot can continue indefinitely: `run` then hands
control back according to its budget, without inventing a negative native
result. This case must not be treated as a completed invocation.

## Encrypted counter

The final equality between the inserted count and the quota triggers the real
host service `114C15C`, virtual `+50`, once. The counter belongs to the
**returned** object, whose 32-bit address is supplied explicitly; no x64 C++
pointer is narrowed to a DWORD.

The base field `+18` is read before each of the two decryptions. Each
computation keeps its own relative value, offset and block. The two plaintext
blocks occupy adjacent buffers: a read at offset 5, 6 or 7 of the first may use
the bytes of the second. The increment wraps modulo 32 bits and modifies the
bytes of the second buffer, which is re-encrypted to its retained
destination. A mapping change after the second read does not redirect this
store.

A store overflowing the second buffer is a modern `counter_layout` refusal;
native stack corruption is not simulated. A missing or too short mapping waits
or fails explicitly. These refusals preserve the identities already modified
and allow no replay of the invocation.

## Bindings and verification

Keep spans, names, key and services alive and unmoved; the correct key is
supplied by the adapter, with no embedded game constant. Serialize accesses.
The reentry guard is not a mutex. Unknown bindings wait only when they are
required; pointers confirmed to be null fail. Captured metadata and reread
data remain distinct during resumptions.

**46 targeted suites pass on both x64 and Win32**, MSVC Release C++latest,
`/W4 /WX /permissive- /utf-8`. The new test performs 123 checks: signed
quotas, insufficient capacity, duplicates, catalog resumption, null identity
loop, errors keeping the prefix, distinct reads, counter offsets and
mappings. The 45 tracked sources and six sensitive files remain unchanged
during validation.

```powershell
cmake --build build --config Release --target vmf_notification_assignment_test
ctest --test-dir build -C Release -R '^vmf_notification_assignment$' --output-on-failure
```

The first fixture resumed a 17-step budget too few times; it stopped before
the end of the 640 records. Only the resumption ceiling of the test was
corrected. Production code did not change to satisfy this test. The context
contains no ordinary allocation; the supplied host may perform some. Neither
this property nor these tests prove the smoothness of the complete game,
displayed commercial menus or a working co-op session.
