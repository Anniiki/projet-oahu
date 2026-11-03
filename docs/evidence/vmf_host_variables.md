# Access to host service variables

## Native evidence

The module reconstructs the read path of 440E00 in the secondary PE,
SHA256 f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed.
Private evidence: reprise-2026-10-31/rea/vmf-host-callback-v2/session/39/43/44,
that is resolution, instructions and 166 bytes PE/Rea/DUMPBIN.
The [official menu corroboration](official_menu_bindings_runtime.md)
now confirms the bytes of this getter, of the three host setters and of
9AB950. This establishes their presence in the process, not their execution.

The native code starts from the root of the supplied movie, resolves with
4491A0, copies the text via 4436A0 then calls MSVCR71::atoi (import 9EC358).
The local buffer is 32 bytes, at ESP_initial-32. An absent path or variable
yields zero.

## C++ contract

read_vmf_host_integer uses the owned root and dictionary, without modifying
the current target of a suspended script. Scope 0 and a present empty key are
valid. A found target without a name is refused before the dangerous native
NULL. Absent backend, closed context, budgets and storage errors remain typed
errors with nested diagnostics.

Numbers are formatted before conversion; links are read at call time. The
copy refuses links longer than 31 bytes without truncation. Flags, frames,
scripts, stacks and values remain unchanged; the lookup may use its internal
scratch. All accesses remain serialised.

vmf_host_integer_from_text accepts the defined ASCII/int32 domain of atoi:
C whitespace, sign, decimal digits, stop at the first other character and zero
without any digit. Thus 1e3 and 1.9 give one; true and 0x10 give zero. The
first NUL or the end of the view terminates the input. A prefix longer than
31 bytes, non-ASCII input and overflows are refused explicitly; no equivalence
to non-ASCII locales or to the historical overflow of MSVCR71 is claimed.

## Validation and follow-up

Six targeted suites pass on x64/Win32: GetVariable (1,034 checks),
host_integer (256), host_write (426), binding_mask (514), SetVariable (814)
and lifecycle (324). The tests also check ordinary allocations, links,
scopes, quotas, closing and a suspended script.
Two initial attempts used push_text, limited to 31 bytes; the correct fixture
uses VmfLinkedText::copy then push_linked and checks its length of 32.
Failures kept, production unchanged for these fixture corrections.

Wiring to the right command handlers remains required. No game callback,
displayed screen, server access or co-op mode is validated here.

## Writes from the root

Additional private evidence: rea/vmf-common-sbdd-v1/session, requests
21..33 and 39..47. 440B00 calls 449240 then 443770 to copy the text;
440AB0 calls 4492E0 then the already reconstructed link setter 4437F0.
440BA0 formats with MSVCR71::sprintf, import 9EC378, format %d at F0A258,
then writes with 443770. Its integers are TEXT, neither floats nor
number-typed variables; 16777217 must remain exact.

write_vmf_host_text copies at most 31 bytes. write_vmf_host_linked_text keeps
the backing and its changes, without truncating long links.
write_vmf_host_integer covers every int32 with a locale-independent decimal
conversion. All three use the root, the 4491A0 classification and the owned
display setters: an absent path has no effect, an empty name is valid, a NULL
name or a full table gives no insertion but sets flag 200 on the found target.
Modern errors keep their nested diagnostics before any mutation.

The first builds detected a signed/unsigned comparison in the mask test;
U suffix corrected. The first write scenario tried to clear 200 via
set_driver_properties, which must precisely preserve it.
The fixture now uses fresh contexts for the refusals and no-ops, with the
same strong expectations. No production logic was modified for these
corrections; failure logs kept.

## Menu data selection

read_vmf_menu_binding_mask reconstructs only the prefix 9AB950..9ABA13:
initial_flags OR 1, then OR of the mask if the root integer is exactly one.
The keys and constants are checked in the instructions and in the PE:

| Key | Mask |
| --- | --- |
| db_hud | 0x2 |
| db_hud_cha | 0x4 |
| db_map | 0x8 |
| db_home | 0x10 |
| db_challenge | 0x20 |
| db_gme | 0x40 |
| db_shops | 0x80 |
| db_drive_in | 0x100 |
| db_ice | 0x200 |

This access modifies no variable or flag and publishes no partial mask on
error. It executes neither the following bindings of 9AB950 nor sbdd.
The Rea batch is closed: eleven bodies/2164 bytes PE/Rea/DUMPBIN, twelve
ranges, nine bytes of gaps excluded. A 15-byte epilogue is checked separately.
Of 2456 bytes read, 2428 are backed by the file; 28 come from the PE's virtual
zero-fill. These zeros are not values observed in the running game.
