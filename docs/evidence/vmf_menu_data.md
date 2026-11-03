# Loading and injection of menu data

## Provenance

The official banks DB.bnk and DB_FR.bnk, read in RAM, provide
TDU_Menus.db and TDU_Menus.fr. Both banks are 624 640 bytes; SHA256:
9e8f656dd95ba32431f57ba2e6557fb347fabbacec43b27ce92633703f82bebb and
9d2d02c363fa0e192c0ea0d1b5b7c317063630521aca6d7be32206a04a4c00b4.
The existing adapter verifies the official PC MC 1.66 A executable copy and
provides the public format constant without displaying it. No asset or
decoding constant is added to the sources.

Additional native evidence: secondary PE
f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed,
rea/vmf-menu-data-bindings-v1. The closed batch of 105 queries verifies 18
bodies, 12 518 PE/Rea/DUMPBIN bytes, 56 ranges and 179 excluded alignment
bytes. The 12 805 bytes read are backed by the file. A
[corroboration in the official process](official_menu_bindings_runtime.md)
now covers nine complete functions, one loader window and its dispatch
table: 2 176 bytes in the executable section and 12 scalar bytes. This
subset does not corroborate the 18 whole bodies of the secondary batch; no
routine is called by the probe.

## Schema and resolution

9AC133 names TDU_Menus, then calls 9AB470 with index 0 and a 24-byte stride.
The count arrives in container+5A0. The table contains 1 119 rows:

| Field | Source type | Native position |
| --- | --- | --- |
| Flash_Name_ | u | 0: identifier, then reference to the name |
| Libelle_ | h | 8: identifier, then reference to the text |
| BitField | b | 16: selection mask |

The dispatch table 9AEE8C links h/u to the same path 9AED4A and b to 9AECF4.
The DWORD at offset 20 is not used by the studied loop. The fields are not
64-bit pointers: two DWORDs carry the ID, then 9AE4D0 replaces the low DWORD
with a 32-bit pointer, keeping the high one.

9AE4D0/9AE600 choose the **first** definition of a 64-bit ID. The French
catalog contains four divergent IDs used eight times by Flash_Name_.
The general unique_value lookup refuses them; this new game adapter keeps
the first definition, without changing the general API. No reference used
by the official table is missing.

VmfMenuDatabase validates names, order and types, then owns the strings and
references. IDs are compared numerically, including their leading zeros.
Malformed entries, overflows and quotas are modern refusals.
The declared domain is ASCII decimal/uint64 for the IDs and uint32 for the
mask; the whole of the legacy converters is not claimed.

## Resumable phase

VmfMenuBindingContext reads the db_* mask once from the root. Each row is
visited in order; a non-zero intersection selects it. The two NULL reference
tests may skip the setter call, but the row is still counted. A present
empty string remains distinct from NULL. The link setter keeps its backing;
a full table or an absent path is not a successful insertion, even if the
host call completes.

The row budget suspends before the next row. Resumption keeps the mask, the
progress and the earlier effects. A modern error keeps the prefix and
forbids replaying the context. Bit 31 of a selected row keeps an unconsumed
needs_indirect_service boundary. This bit is inactive in the 1 119 official
rows observed.

rows_finished only means the end of this loop.
The [finalization context](vmf_menu_finalizers.md) now consumes this phase
to run cache, units and 9AB830, without replaying the rows.
The receiver services and the FSCommand wiring remain necessary before
sbdd is considered executed. The suspended script, its stack and the frames
are not advanced by these APIs.

## Validation

Eight targeted suites pass on x64 and Win32, including 325 checks for this
module: first definition, guards, order, resumption without replay, frozen
mask, errors, display identity and failures of every creation allocation.
The observed run paths do not allocate on the ordinary heap.

The additional diagnostic, 303 checks per architecture, loads the real
assets into an isolated C++ dictionary:

| Mask (hex) | Rows visited | Setter calls | Final entries |
| --- | --- | --- | --- |
| 1 | 1119 | 196 | 196 |
| 3 | 1119 | 487 | 487 |
| 3FF | 1119 | 1118 | 1114 |

Local execution, without launching the commercial game:

    cmake --build build --config Release --target vmf_get_variable_test
    ctest --test-dir build -C Release -R '^vmf_menu_data$' --output-on-failure
    .\build\Release\vmf_get_variable_test.exe --menu-data-official "<installation>"

Private evidence is in vmf-menu-data-runtime-v1. This diagnostic validates
neither display, input, authentication, multiplayer nor co-op.
