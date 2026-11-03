# Official corroboration of the presentation service

## Bounded capture

The exclusive ui-vmf-help-message profile of tdur_probe_code reads four
fixed executable ranges, 1 065 bytes in total, covering three complete
AddressSets:

| Procedure | Ranges | Bytes |
| --- | --- | --- |
| 9A03E0 | 9A03E0/512 and 9A05E0/301 | 813 |
| 441180 | 441180/128 | 128 |
| 441560 | 441560/124 | 124 |

No manager field, world pointer, account parameter, scalar or unrelated
routine is followed. The compiled checks bound each read to 512 bytes and
enforce the budget and the absence of overlap. The PID, FILETIME and image
identities are correlated with the launcher reports. These are successive
reads, not an atomic snapshot.

The [static evidence](vmf_help_message.md) ties these ranges to Rea/Ghidra
and DUMPBIN. The functional data, 385 bytes read on disk, match both files
and include the NUL byte of the empty string at F035DF.
The Rea read 441180/512 includes 384 bytes outside this procedure; only its
128 AddressSet bytes are retained in the body evidence.

## Real trial

Isolated trial through coop-anything/gameslot:
<isolated-copies>/slot0/20261031T195112-31208.
PID 31120, FILETIME 134379499098188933, base 400000; duration 100.186 seconds.
All four ranges are readable and equal to the studied secondary.
The two 9A03E0 ranges also equal the primary on disk; the two helpers
differ on disk and match the code observed in memory.
The cause of these differences is not inferred.

The Job is bounded to one process, the official copy is verified and
locked, then stopped at the deadline. The 6 697 reference files and
17 savegame files remain unchanged. The slot is released.
The two tracked repository guideline files, executables and banks are
preserved.

The launch uses the temporary borderless D3D9On12/DXGI diagnostic bridge.
It does not validate the final native D3D12 renderer. No UI action, no
protection modification and no forced native call is performed.
The presence of the bytes does not prove that these routines were executed,
that a tutorial was displayed, or that an online profile or co-op works.

## Private evidence

vmf-help-message-official-v1 contains the launch, snapshots, comparison
and artifact hashes. vmf-help-message-runtime-v1 contains the port, its
validations and reviews. The later fix preserving the callbacks concerns
the C++ reconstruction; the captured bytes and the probe did not change.
The native comparisons and the C++ tests have distinct scopes.
