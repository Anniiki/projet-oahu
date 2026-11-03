# Tutorial and help catalog

## Data and provenance

The module reads the TDU_Tutorials.db and TDU_Tutorials.fr members of the
official copy's banks in memory. The existing adapter verifies the official PC
MC 1.66 A executable before providing the format constant, without displaying it.
No commercial text, bank or decoding constant is added to the repository.

An independent inventory, then its re-execution, record the following:

| Member | Index | Offset | Size | Content |
| --- | --- | --- | --- | --- |
| TDU_Tutorials.db | 11 | 508384 | 3464 | 92 rows, 5 columns |
| TDU_Tutorials.fr | 11 | 441392 | 81938 | 562 entries |

The bank hashes are recorded in
[menu data loading](vmf_menu_data.md). The private validation data
stays in reprise-2026-10-31/vmf-common-help-runtime-v1.

## Schema and identities

| Column | Type | Native position |
| --- | --- | --- |
| Text_ID_ | u | +0: routing key |
| TextIndex_ | u | +8: reference kept without interpretation |
| Libelle_ | h | +10 hex: text |
| File_ | u | +18 hex: media name |
| Time_cent | i | +20 hex: integer kept without unit conversion |

The observed 40-byte stride and the four u/u/h/u fields use the
[9AE4D0/9AE600](vmf_menu_data.md) resolution. Each first definition
of a 64-bit numeric ID has its backing. Decimal aliases of the same ID,
including those with leading zeros, share that reference; two distinct IDs
with identical content keep two distinct references.

VmfTutorialDatabase::find_help reproduces 9827B0's selection of the first
exact name. find_tutorial keeps 7FBEF0's filtering: skip every row
whose text address is that of the catalog sentinel, then continue through
the duplicated keys. The "none" content does not replace this identity
comparison. An absent sentinel ID corresponds to a known NULL lookup.
Service IDs are supplied by the caller, not added to the catalog.

## Modern limits

The constructor requires this exact schema, decimal ASCII/uint64 tokens,
four resolved references and a Time_cent within signed32. It refuses absent
or malformed references that the native code could turn into dangerous
pointers. This domain does not claim to reproduce all the legacy
converters. Alignment fields and time units are not inferred from their
names.

Quotas bound rows, entries, token length and retained extents. Repeated
references are counted, in addition to the catalog backing; this count is
not the allocator's exact peak memory. Lookups create no
allocation on the ordinary heap. References own their storage; a moved-from
object refuses lookups instead of simulating an empty bank.

## Validation

Synthetic tests cover numeric aliases, duplicates, distinct identity with
equal text, continuation after the sentinel, letter case, errors, quotas and
the refusal of each construction allocation. The official diagnostic
separately checks each reference of the 92 rows and the 32 slots of the native
routing. Twenty slots find a row in this bank; the others keep their real
absence, with no invented text. On x64 and Win32, 72 synthetic checks and
5 361 checks on the official data pass.

    ctest --test-dir build -C Release -R '^vmf_tutorial_data$' --output-on-failure
    build/Release/vmf_tutorial_data_test.exe --official "<installation>" 5373334 5682783

The arguments are the IDs observed in the services of this version. They
are neither an activation key nor a decoding constant. The tests cover the
data and the C++ decisions; they do not launch the commercial game. The
presentation effects of 9A03E0, the tutorial transitions, the media loading
of 982270 and the complete receiver remain to be wired before the help can
be declared playable.
