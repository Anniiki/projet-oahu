# Formatter comparison and copy: 451AA0 / 451A70

These two C++ helpers reconstruct the byte operations used by the
formatter `717BF0`, without allocation or callbacks.

## Counted comparison

`compare_vmf_notification_prefix()` returns zero when the requested count
is consumed. Otherwise it returns `-1` if the current reference byte is
NUL, and `+1` in every other case. Thus `A/Z` and `Z/A` can both return
`+1`; two empty strings with a positive count return `-1`.

The reference is required even for a zero count. If its first byte is
non-zero, the candidate is required before the count test; an empty
reference never reads the candidate. These phases preserve the observed
guards.

## Progressive copy

`copy_vmf_notification_prefix()` copies forward until NUL or the requested
count, then returns the number of bytes written. It writes neither a
terminator nor padding. A zero count does not access the source; an empty
source requires no output space.

Memory overlap follows the successive loads and stores: a destination
offset by one byte into the source can propagate the first byte. An output
that is too short is refused at the first store outside the span, with the
offset and the previous stores preserved. The end of the views implies a
NUL; the storage must remain stable and accesses serialized.

## Evidence and validation

The complete bodies of 63 and 36 bytes match the **loaded primary**
MC 1.66 A in the [earlier official trial](official_notification_helpers.md).
The bytes of the primary file on disk differ; no on-disk match and no new
targeted invocation are claimed here.

MSVC Release C++latest, `/W4 /WX /permissive- /utf-8`: **35 targeted suites
pass in each of the x64 and Win32 architectures**, including 33 checks of
these helpers. They cover the guards, zero and large counts, NUL, high
bytes, partial errors and three forms of overlap. Eight frozen sources and
six sensitive files remain unchanged during validation. Private evidence
and logs:
`<private-evidence>/reprise-2026-10-31/vmf-notification-prefix-runtime-v1/`.

```powershell
cmake --build build --config Release --target vmf_notification_prefix_test
ctest --test-dir build -C Release -R '^vmf_notification_prefix$' --output-on-failure
```

Modern refusals define the domain of absent pointers and bounded spans. The
complete formatter, its hosts and the playable menus remain to be wired;
these helpers do not validate a multiplayer or co-op session.
