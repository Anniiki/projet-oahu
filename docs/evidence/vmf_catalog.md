# Localized text catalog

`tdur_vmf_catalog` reconstructs the lookup `9AE600` and the selection `9AF890`
in modern C++, then provides an owned catalog built from the real
`DatabaseStrings`. Both complete bodies, **95 bytes**, match the official
PC MC 1.66 A primary executable, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
This on-disk comparison does not prove that they are invoked in game.

## Lookup and selection

`find_vmf_catalog_text()` keeps the signed count, the first matching 64-bit
identifier, zero included, and the guarded reads: low word, high word only
when necessary, pool binding, then offset. It reads neither the following
opaque word, nor the characters, nor the records after the match.
A zero or negative count requires no binding. An exhausted budget, a missing
binding or an out-of-bounds read remain distinct from an absent ID.

`select_vmf_catalog_text()` computes `selector * 0x4c + 0x48` **modulo 32 bits**
before looking for an explicitly bound descriptor. Selector aliases therefore
remain reachable on x64. Only a duplicate of the selected descriptor is an
error. This typed registry replaces the original DWORD addresses; the functional
meaning of the selector and the global `11F2CDC` remain to be established.
A raw view borrows its storage; its end bound serves as an implicit NUL for
later consumers, without scanning ahead for a terminator.

## Data and lifetime

`VmfLocalizedTextCatalog::from_catalogs()` accepts already decoded sources.
It keeps the first decimal definition of each ID, including aliases with
leading zeros. The values of duplicates are not reread. Texts are normalized
at the first NUL, with a pool and one backing retained per ID; the quota
counts **both copies**, terminators included. The older menu and tutorial
catalogs keep their own contracts.

The returned references survive the move or destruction of the sources and of
the catalog. The lookup does not allocate; construction bounds tables,
entries, identifiers and storage, and translates allocation failures.

## Validation

**37 targeted suites pass on each of the x64 and Win32 architectures**,
in MSVC Release C++latest with `/W4 /WX /permissive- /utf-8`: 133 synthetic
checks, 163 with the local bank opened read-only. Eleven frozen sources and
six sensitive files are preserved during validation.

```powershell
cmake --build build --config Release --target vmf_catalog_test
ctest --test-dir build -C Release -R '^vmf_catalog$' --output-on-failure
.\build\Release\vmf_catalog_test.exe --installation '<installation>'
```

The test reads `DB_FR.bnk/TDU_Menus.fr` and compares twelve real IDs without
exporting their texts. Its selector `7` is a **declared synthetic** binding,
not evidence of a native association with that bank or that language.
Twelve independent cases from an earlier review cross-check the results and
the order of the reads; their Python model does not certify the general
overflow of the 32-bit cursor.
Private evidence: `<private-evidence>/reprise-2026-10-31/vmf-catalog-runtime-v1/`.
The complete formatter, its hosts, the playable menus and co-op remain to be
connected.
