# Live formatting cache: 717420

`VmfNotificationFormatCache` carries the three branches of the notification
text cache and calls the real VMF services. This cache owns 32 names of 64
bytes and 32 values of 512 bytes. It is distinct from the ten 82-byte text
parameters already reconstructed.

## Lookup, copies and binding

The routine first looks for the first matching name among **all 32
slots**. It looks for an empty slot only when no match exists. Duplicates
therefore remain possible and the first definition wins.

| Branch | Cache modification | Host call |
| --- | --- | --- |
| Name found | Value copied over 512 bytes, padding then NUL at 511; name untouched | Real text binding to the slot value |
| First empty name | Name copied over 64 bytes then NUL at 63; then value over 512 then NUL at 511 | Same binding, with the stored name |
| Cache full | No modification; the input value is not read | Copy of the owned fallback text at F39F50, under the original input expression |

A repeated long name does not automatically match its copy truncated to 63
bytes: it may occupy several slots. The copies remain operations on raw bytes,
without Unicode normalization.

## Permanent storage and errors

Each slot preallocates a single shared 512-byte backing. Its contents change in
place; its identity stays the same. Important case: an empty name with value A
may already bind slot 0 to the display. The next insertion of X/B reuses that
slot, and the old binding then sees **B**. Allocating a new backing for that
insertion would wrongly preserve A.

References keep their backing after the cache is moved or destroyed. The
existing common storage is defined in an internal header; no factory for a
native pointer or for internal storage is exposed publicly. `write()` allocates
no ordinary memory in the verified scenarios.

The factory supplies an empty modern cache or imports exactly 32 owned records
whose names are terminated within their 64 bytes. It does not claim to
reconstitute the constructor of the original singleton. Bounded views imply a
NUL at their end; they must remain readable and stable. Accesses to the cache,
the displays and the shared texts are serialized.

A partial overlap is refused before the affected copy. A missing source or a
backend error keeps the previous copies, for example the name written before
an error on the value. No implicit rollback or replay. `host_call_completed`
may correspond to a native no-op, without insertion. The global toggles and
the profiling of the native lock are not reproduced by these display-owned
services; their concurrent parity is not established.

## Validation on 2026-11-01

The complete body of **837 bytes** matches the official MC1.66A primary,
SHA-256 `3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The constants are backed by separate evidence. No new game trial was run here.

MSVC Release C++latest, `/W4 /WX /permissive- /utf-8`: **32 targeted suites
pass in x64 and Win32**, including the shared VMF storage regression tests.
The cache scenario comprises 340 checks, including the common PC 2.8 and
Xbox 2.5 fixtures. Genuinely live bindings, cache destruction, padding,
truncation, duplicates, import, partial errors and fallback are verified. The
five byte-level scenarios from an earlier review were also replayed with a
different loop; this byte-level model does not simulate the commercial process.

```powershell
cmake --build build --config Release --target vmf_get_variable_test vmf_stack_test
ctest --test-dir build -C Release -R '^(vmf_notification_format|vmf_value_stack|vmf_host_write)$' --output-on-failure
```

Nine frozen sources, six preserved sensitive files and the logs remain outside
Git in `<private-evidence>/reprise-2026-10-31/vmf-notification-cache-runtime-v1/`.
The first Win32 failure came from signed constants in the assertions; they
are corrected without relaxing the warnings. Parent, complete formatter,
commercial menus, final native rendering and real co-op remain to be
connected.
