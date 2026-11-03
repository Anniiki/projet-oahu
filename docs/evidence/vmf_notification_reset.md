# Notification reset: 891900, 894CC0

The C++ runtime resets the encrypted states of a notification, then removes
every matching identity from a table. It reuses the
[shared primitive and the identity lookup](vmf_notification_identity.md).
Loading the real data, their key and their host remains required.

## Distinct fields and preservation

`VmfNotificationResetRecord` has the logical fields covering a native record
of 128 bytes. The offset names document their provenance; they impose neither
a C++ binary layout nor a network packet format.

| Native field | Effect of 891900 |
| --- | --- |
| Encrypted block 18 | Decrypt, set only the first DWORD to zero, re-encrypt |
| 20, 24, 28 | Write FFFFFFFF, byte 2, then zero |
| Encrypted block 2C | Decrypt, set only the first DWORD to zero, re-encrypt |
| 60 | Write the bits of the positive double zero |
| 38, 4C, 50, 68, 6C | Write FFFFFFFF, in that order |
| 34, 54, 58 | Write zero words, in that order |

`reset_vmf_notification_record` carries this routine and returns 0, like EAX
at its exit. The second **decrypted** word of each block stays intact;
keeping a ciphertext word or systematically encrypting zero/zero would be
incorrect. The encrypted identity 70, the following DWORD 78 and all opaque
bytes stay unchanged. The native map writes 65 bytes and preserves 63.

Blocks 18, 2C and identity 70 remain three independent values. The
encryption uses an explicit four-word key, without adding CBC, an IV or the
default key of the database. The key must stay valid, stable and distinct
from the modified storage; data accesses are serialized. These functions do
not take a lock themselves.

## Removal of every match

`clear_vmf_notification_identities` carries 894CC0. The routine decrypts the
**640** identities in turn and compares their two words against the arguments
captured by value. For each match, it performs the real reset, encrypts
zero/zero into the identity, then sets DWORD 78 to zero.

The walk continues after each match, up to index 639. Non-matches stay
entirely intact; a possible element 641 supplied by the caller is ignored. An
identity encrypting zero/zero is still examined: a ciphertext of eight zero
bytes is neither that result nor an empty-slot marker. A zero/zero request
can therefore find identities already reset during an earlier call.

A truncated span produces a modern error at the first missing access. The
modifications of the earlier matches are kept. The report states the records
walked and modified, including in the error; it does not replace the native
return with a business boolean. An error does not authorize an automatic
replay. No buffer, key or commercial resource is embedded.

## Evidence and validation of 01/11/2026

The two complete bodies, **277 bytes**, match the official MC1.66A primary on
disk, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
PE, Rea and DUMPBIN are cross-checked. The eight-byte constant at F41780 is
also verified as zero in both images; no read of that constant in a live
process is claimed here.

MSVC Release C++latest, `/W4 /WX /permissive- /utf-8`: **26 suites pass in
x64 and Win32**, including 53 checks from this batch. Complete projection of
the 128 bytes, high words at boundary values with two synthetic keys, matches
at indices 0 and 639, half-matches, zero request, repetition, different keys,
raw zero ciphertext and errors after a prefix are verified. The tests compose
the primitive already verified against public vectors; they do not constitute
a new independent cryptographic oracle.

```powershell
cmake --build build --config Release --target vmf_notification_reset_test
ctest --test-dir build -C Release -R '^vmf_notification_(reset|identity|queue|record)$' --output-on-failure
```

Six frozen sources, six preserved sensitive files and the logs are recorded
outside Git in the private evidence folder
`reprise-2026-10-31/vmf-notification-reset-runtime-v1/`. An independent
review reported no concrete defect. A second review compared the eight
sources against its initial table and verified the five C++ copies and the
five hashed outputs. The implementer did not supply the requested final
source table; the current comparison was performed by that review. The
oracles assume blocks actually encrypted with the supplied key. That review
also ran the tests.

No new game trial or forced native call for this batch. The parent services
and the menus remain to be connected; the online profile, the final native
D3D12, a real co-op session and the smoothness of the complete client remain
to be verified separately.
