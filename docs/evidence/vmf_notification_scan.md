# Notification parameter pre-scan: 716F60

`scan_vmf_notification_tokens()` reconstructs the traversal performed by
routine `716F60` before formatting. It reuses the `451C90` encoder to produce
the `C2 A7` delimiter from the byte `A7`, then the existing ASCII integer
converter for normalized decimal prefixes. It modifies no text.

## Results and ordering

Only an encountered index equal to the current count plus one advances that
count. The maximum encountered is kept separately. The native result `-1`
means that this maximum exceeds the count, and remains a **valid value** of
the modern API; bounded refusals use `std::unexpected`.

| Parameter bodies encountered | Result |
| --- | --- |
| `1`, `2` | `2` |
| `2`, `1` | `-1` |
| `2`, `1`, `2` | `2` |
| `0`, `-9`, `10`, `word` | `0` |
| `1abc`, `02suffix` | `2` |

The signed comparison `index <= 9` makes the reachable final count lie
between zero and nine. The native clamp at ten does not allow ten to be
reached. There is no new traversal from the start to reorder the indices.
The `%d` prefix may count a token that the formatter later treats as a named
parameter; the two functions remain distinct.

## Bytes, bounds and conversion

Native steps are one, two or three bytes depending on the leading bits; the
other patterns, notably `F0`, advance by one byte. This is not a Unicode
decoder or validator. NUL terminates the outer loop, while the search for the
closing delimiter may cross an inner NUL.

The supplied backing must remain stable and readable. Its end implies a
virtual NUL; advancing beyond it produces a modern refusal with an offset. The
conversion covers ASCII spaces, sign, digits and leading zeros, without
imposing the 31-byte buffer limit of another VMF host. Signed overflow and
locale-specific behaviour is not inferred.

A zero return from `sscanf` and EOF are distinct: after EOF, its local integer
keeps a previous known assignment. Without a known assignment, the historical
routine would read an uninitialized value; the modern API returns
`unknown_scan_value`. No variadic x86 pointer is reinterpreted. The historical
CRT also returns EOF for a sign immediately followed by a NUL; a sign followed
by a space produces zero assignments.

## Evidence and validation

The complete body of **575 bytes**, excluding alignment, matches the official
PC MC 1.66 A primary, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The `%d` constant is compared in both images and against the Rea result. The
[import trial](official_formatter_imports.md) confirms `sscanf` at cell
`9EC370`, without a targeted invocation of the pre-scan.

MSVC Release C++latest, `/W4 /WX /permissive- /utf-8`: **33 targeted suites
pass on each of the x64 and Win32 architectures**, including 138 pre-scan
checks. Ordering, duplicates, the 24 permutations of four indices, int32
limits, truncated backing, crossed NULs and the preserved temporary value
are verified. Eight frozen sources and six sensitive files remain unchanged
during validation. Private logs and evidence:
`<private-evidence>/reprise-2026-10-31/vmf-notification-scan-runtime-v1/`.
A Win32 helper separate from the game also ran 16 cases against the same
verified `MSVCR71.dll` file, confirming the returns and the preservation of
the integer. The sign/NUL case corrected the first port before commit; the
first validations at 130 checks and the subsequent logs are kept.

```powershell
cmake --build build --config Release --target vmf_notification_scan_test
ctest --test-dir build -C Release -R '^vmf_notification_scan$' --output-on-failure
```

The formatter, its dispatch to the cache, the commercial menus and co-op
remain to be connected. These tests do not constitute a playable trial of the
client.
