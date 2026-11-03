# Encrypted identities: 6DDD70, 6DDDE0, 8911A0

The C++ runtime provides the real identity decoding used in the
notification branch 89E330. It reuses the primitive already present in
the data reader, now extracted into `tdur_xtea_words`. Loading these
identities and their key from the game state remains to be wired; no game
constant is embedded by this component. The
[projection onto owned records](vmf_menu_display_selection.md) now reuses
the same scan, without copying the 640 payloads.

## Shared primitive

`encrypt_xtea_words` ports 6DDDE0 and `decrypt_xtea_words` ports
6DDD70. Each function receives two 32-bit words and an explicit four-word
key. The 32 rounds keep the original additions, subtractions, shifts and
key indices, with modulo 2^32 overflow even on x64. The two input words
are captured before the key reads.

These operations work on words. The adapters remain responsible for byte
order and for the block mode of operation. `decode_database_resource`
calls the shared decryptor, then keeps its earlier CBC XOR, IV and
little-endian conversions. The identity scanner uses the block directly,
without adding CBC/IV. It does not assume that the identity key is the
database key.

## Real lookup

`find_vmf_notification_identity` ports 8911A0. Its span holds the owned
8-byte encrypted payloads that correspond to the native fields at
owner+90, stride 80 hex; the other bytes of the records are not
reconstituted by this logical type.

The function decrypts at most 640 records in sequence, compares the two
words against the requested low/high, then returns 1 on the first match.
No match among the 640 yields 0; any 641st element supplied is ignored.
The data and the key must remain valid and stable, with serialized access
during the call.

A span that is too short produces a modern error at the missing access,
after the preceding reads. A match within the prefix can therefore
succeed without requiring the later records. The found index and the counters are
modern diagnostics. Missing storage never becomes a successful negative
answer. The inputs remain unchanged.

## Validation on 01/11/2026

MSVC Release C++latest, `/W4 /WX /permissive- /utf-8`: 23 suites pass on
x64 and Win32, including 49 new checks. The data reader and menu service
regression tests are included. The tests cover four
[public Bouncy Castle vectors](https://github.com/bcgit/bc-java/blob/6f09530ed852caecd8386f1189b4b84000495015/core/src/test/java/org/bouncycastle/crypto/test/XTEATest.java),
with two distinct keys, then the 32-bit limits, both halves of the
identifiers, duplicates, table bounds and storage errors. Roundtrips
complement the independent vectors.

```powershell
cmake --build build --config Release --target vmf_notification_identity_test database_asset_test
ctest --test-dir build -C Release -R '^(vmf_notification_identity|pc_database_assets)$' --output-on-failure
```

The three complete bodies, 292 bytes, match the official PC MC 1.66 A
primary on disk, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
PE, Rea and DUMPBIN are cross-checked; nine frozen sources and six
preserved sensitive files are recorded outside Git in
`<private-evidence>/reprise-2026-10-31/vmf-notification-identity-runtime-v1/`.
An independent review reported no concrete defect across the nine
sources (71132 B); the initial, final and current hashes match. This
review is accepted within the documented domain, and the preconditions
of the table-bound oracle are completed: all preceding records must be
non-matching.

No new game trial and no forced native call were performed for this
batch. The complete constructor, the notification hosts and the menus
remain to be wired. These results validate neither final native D3D12
rendering, nor online profile, nor working co-op or smoothness of the
complete client.
