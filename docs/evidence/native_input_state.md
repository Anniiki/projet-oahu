# Native operations on input states

## Independent sources

Routines 433DF0/87B and 434AB0/40B come from private exports `05/06`
in `reprise-2026-10-31/rea/vmf-focus-services/session/`. The subject is
the secondary PC Paradise f452afcd…de6ed; `exact-byte-validation.json`
compares Rea/PE/DUMPBIN bytes. No keyboard/gamepad role is inferred
from identifier 0.

Independent oracles were frozen before this C++ code: 16 lookups and
8 resets, reread from the assembly. They were translated into
standalone numeric fixtures; generation was reproduced without
writing and all 4 032 bytes were checked before adoption. Oracle source SHA-256
`d3b42fea58edeeb561ab493c829a8fbc94de7d0ece344256b1e1c4e87ef5ade1`.

## Lookup 433DF0

Native code traverses count[+84] pointers from base[+8C], compares the
`element+4` DWORD and selects the requested ordinal occurrence (base 0).
The first NULL pointer immediately ends traversal. ID 0 and index-0
results remain valid values.

`find_native_input_slot(span<optional<uint32_t>>, id, ordinal)` returns
an `optional<size_t>`. The span defines the valid prefix; no native
capacity or commercial address is introduced. The original body reads
the first slot before checking count 0 and preloads the next before
the end test. The bounded C++ does not reproduce these out-of-prefix accesses.

## Reset 434AB0

The routine traverses count[+0C] 84-byte records from base[+44].
`reset_native_input_records(span<NativeInputRecord>)` retains their raw
storage and applies only:

- byte 8: preserve the low four bits;
- byte 9: clear bit 0;
- hexadecimal bytes 30..33: write four zeros.

Every other byte and every record outside the span remain intact.
No value is loaded as float and no calculation on NaN/negative zero
is performed. Neither API allocates memory or triggers a callback.

## Verification and limitations

`native_input_state_test` checks the 24 frozen cases, canaries across
the 84 bytes, idempotence and all 65 536 flag pairs. On Windows, an
inaccessible page follows the final slots/records; empty and unsuccessful
lookups verify the absence of native read-ahead accesses.
**67 600 checks pass per architecture, x64 and Win32.**

These helpers prepare the service called by 44A3B0/44A440; they are
neither the VMF fields' 32-slot registry nor an SDL adapter. Virtual
+4/+8 effects, interactive focus and actual chat-popup closure remain
to be connected. Window-focus loss must not suspend simulation;
no new SDL focus policy is added here.
