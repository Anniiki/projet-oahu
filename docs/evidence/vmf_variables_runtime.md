# Official corroboration of the VMF dictionary

## Result and identity

The **12 ranges, 1479 bytes**, representing the eleven procedures of the
[C++ dictionary](vmf_variables.md), match exactly the secondary analyzed in
Rea. None of these ranges equals the official executable on disk at the same
VAs; the cause of this difference is not determined by this trial. This is
evidence that the code is present, not that it is called or of its behavior
in game.

Official PC MC 1.66 A: SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Rea secondary:
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.

Private batch: `<isolated-copies>/slot0/20261031T063150-35772`, target
PID 34920, creation FILETIME 134379019452590151, image at `00400000`.
The executable actually observed is the private copy
`slot0/20261030T035944-25460/game/TestDriveUnlimited.exe`, verified and locked
with the official SHA. PID 35772 belongs to the Python observer;
the source installation remains `<installation>`.

## Exclusive ranges

The compiled profile `ui-vmf-variables` of `tdur_probe_code` requests only:

| Function | Inclusive VAs | Bytes |
| --- | --- | --- |
| Creation | `443320..443386` | 103 |
| First free slot | `443410..44342F` | 32 |
| Typed copy | `443430..4434A6` | 119 |
| Insertion, part 1 | `4434B0..44350C` | 93 |
| Insertion, part 2 | `443510..4435C1` | 178 |
| Lookup | `4435D0..44361D` | 78 |
| Push | `443620..44369A` | 123 |
| Text copy | `4436A0..4436F9` | 90 |
| Number setter | `443700..443766` | 103 |
| Text setter | `443770..4437ED` | 126 |
| Link setter | `4437F0..443856` | 103 |
| Cleanup | `4438D0..443A1A` | 331 |

The three bytes `44350D..44350F`, outside the AddressSet of the insertion, are
excluded. The hash 443390, already corroborated separately, is not read again.
No constant, variable table, free address or pointer chain is requested.
The captures are successive observations, not an atomic snapshot.

## Source, checks and preservation

Rea resumed the verified snapshot `vmf-text-services`, resolved the eight
missing bounds and exported the twelve ranges. **23 successful requests**; the
1479 bytes agree with the secondary PE and DUMPBIN. The three earlier
resolutions are reused after a fingerprint check. The artifacts remain
private in `reprise-2026-10-31/rea/vmf-variable-dictionary/`. The new closed
snapshot has SHA-256
`52f1c2a24083ccf57939557f8e9cae52e22f0948b464cf5e00fe9ad968177cae`.

The probe compiles and passes its self-test on x64 and Win32; it is not
published in this repository.
During the trial it does not write into the target and does not change its
protections. The reference context remains instrumented and x86, with the
requested display `d3d12_on12`/borderless. This does not demonstrate a native
x64/D3D12 final client. No menu, keyboard or mouse action was sent.

After 100.218 seconds, the owned Job reaches its expected limit (1460), then
the slot is released. The observer audits report **6697 reference files and
17 source saves unchanged**. The parent verifier checks the receipts and the
agreement of the save fingerprints; it does not claim a second mass audit
that it did not perform.

`vmf-variables-v1/runtime-parent-comparison.json` verifies the twelve
requests, the individual fingerprints, identity, audits and end of the Job,
and links 28 artifacts by SHA-256. Report fingerprint:
`8608c609680da9cde35a33f5786c5ce225549eb38d12a070b9c54ad3da72bdb9`.
The compiled probe and its two sources are archived in the private batch to
preserve their identity after future rebuilds. No commercial binary and no
captured byte is committed.

The ordered sum of the captured ranges (gap excluded) has SHA-256
`0daeee8e47d8d1f7da19742dc36b85db70480142447bdd4cef793c3cfe2e0f0c`.
This corroboration and the 1779 per-architecture service checks are
complementary. Neither validates yet the wiring of the menus, multiplayer or
co-op.
