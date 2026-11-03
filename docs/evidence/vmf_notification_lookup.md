# Named parameter lookup: 716C40

`find_vmf_notification_text()` searches the ten text slots already
reconstructed. Each slot holds an 18-byte key and a 64-byte value. The
function returns a typed index of the first matching slot; the absence of a
match replaces the native NULL pointer.

## Comparison and access

Keys whose first byte is NUL are skipped. For every other key, the routine
computes its length and then compares exactly that prefix of the token. The
`strncmp` comparison is case-sensitive. The real
[binder](vmf_notification_text.md) appends `C2 A7` to its key; a token must
therefore include that suffix to find it. Raw imported keys keep their own
behavior: a key `A` can match `ABrest`.

Duplicates keep the priority of the first slot. No check is performed on the
following slots after a match. Values are neither read nor copied; a
non-terminated value therefore does not prevent this lookup from succeeding.
Its later use remains separate work.

A token is only required at the first comparison of an active key: an
empty table can return no match without one. A key found without a NUL within
its 18 bytes is refused before comparison, at the length phase. This modern
refusal avoids reading beyond the historical key. A short view implies a NUL;
high bytes stay raw. The storage must remain alive and stable, with serialized
accesses until the returned index is used. No allocation and no mutation.

## Evidence and validation

The 176 bytes of the complete body match the official PC MC 1.66 A primary
executable, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The [separate four-byte trial](official_formatter_imports.md) confirms the
loaded `strncmp` export for the cell `9EC374`. It does not demonstrate the
execution of the lookup itself.

MSVC Release C++latest, `/W4 /WX /permissive- /utf-8`: **34 targeted suites
pass on each of the x64 and Win32 architectures**, including 68 lookup
checks. They use the real binder and the real clear, the ten indices,
duplicates, imported prefixes, case, high bytes, NUL and ordered errors. The
eight frozen sources and six sensitive files remain unchanged during the
validation. Private logs:
`<private-evidence>/reprise-2026-10-31/vmf-notification-lookup-runtime-v1/`.

```powershell
cmake --build build --config Release --target vmf_notification_lookup_test
ctest --test-dir build -C Release -R '^vmf_notification_lookup$' --output-on-failure
```

The index is a modern representation, with no reconstruction of the native
owner's ABI. The formatter and its special parameter sources remain to be
connected; commercial menus and co-op are not yet playable.
