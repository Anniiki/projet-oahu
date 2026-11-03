# Notification construction: 88E780, 8956C0, 895800

The C++ runtime builds and copies notifications with their ordered effects.
`VmfNotificationRecord` owns its fields and buffers; its offset names document
the provenance without imposing an x86 ABI or a network format. Loading the
real data and the callers remain to be connected.

## Partial initialization and construction

`initialize_vmf_notification_record` carries 88E780: 2,301 native bytes are
written and 155 preserved, including the tail of the title and the twelve opaque
bytes. Descriptor 940 receives FFFFFFFF. A global clear would change these effects.

`populate_vmf_notification_record` carries 8956C0 and first reinitializes the
destination. Scalar arguments and view metadata are captured by value; the
contents of the views stay alive during the callbacks.

| Field | Behavior |
| --- | --- |
| Language 89C | Explicit value, or two calls to the language service for FFFFFFFF |
| Header | Two DWORDs, then reduction of the third argument to a WORD |
| Title A | Copy with padding over 144 bytes, then last byte NUL |
| Body 9A | Optional copy with padding over 2,048 bytes; NUL forced at index **1,903** |
| Descriptor | If present, forward copy of 40 DWORDs then write of 940 |
| Texts 96C, 95C | Copy of the prefix and the NUL; tails preserved |
| Footer | Three DWORDs, then reduction of the two flags to one byte |

The body NUL corresponds to relative address 809, that is 809−9A=76F hex.
It lies inside the buffer; the following bytes may remain non-zero.
A NULL body or descriptor skips its conditional writes, including the values
that the language service would have modified after initialization.

The small texts must fit with their NUL in 32 and 16 bytes. An input that is
too long or NULL produces a modern error at its phase, keeping the earlier
effects. The bounded copies accept disjoint or identical storage; the small
texts also accept backward overlap. Dangerous forward overlap is refused
explicitly.

## Concrete language service

`refresh_language` and `run_language` expose 898B40 by reusing the unit
machine of the menu finalizer. A non-empty cache completes without services.
Otherwise the source prefix and its NUL are copied, the tails stay intact,
then the actual unit effects are executed: mode, callback removals in the
order A,A,B, flags, parameters, texts and menu lookups.

Waits and budgets keep the prefix; a resumption does not copy the source
again. The standalone getter requests neither lines, nor root display, nor the
localized label `WhatLanguage`. Its views cover the 64 owned bytes of the
cache, including the bytes after a first NUL. This capability is a modern limit.

`VmfNotificationLocaleLanguage` runs this service with its real bindings.
A missing dependency yields a terminal error that preserves the effects; the
adapter does not restart an incomplete invocation through an already filled cache.
The referenced locale and inputs must stay alive, stable and serialized.

For the automatic language, the two invocations are distinct. The reads of
the first byte of the first view and of the second byte of the second view take
place **after the second call**. Each byte is sign-extended before the shift
and the 32-bit OR. An empty source and an empty cache trigger two passes.

## Copy by reconstruction and validation of 2026-11-01

`copy_vmf_notification_record` carries 895800: the scalars are captured, then
the constructor receives live views into the source. It does not copy the
opaque bytes and reinitializes 990. In a self-copy, the buffers are therefore
read after their reinitialization; a raw assignment would give another result.

MSVC Release C++latest, `/W4 /WX /permissive- /utf-8`: **24 suites pass on
x64 and Win32**, including 44 checks from this batch. The cases cover the full
initialization mask, the copies, the signed and live reads, the unit effects,
the waits, moves and partial errors.

```powershell
cmake --build build --config Release --target vmf_notification_record_test
ctest --test-dir build -C Release -R '^(vmf_notification_record|vmf_menu_finalizers)$' --output-on-failure
```

The three complete bodies, 593 bytes, match the official primary PC MC 1.66 A
executable on disk, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The PE/Rea/DUMPBIN evidence, six frozen sources and six preserved sensitive
files are recorded outside Git in
`<private-evidence>/reprise-2026-10-31/vmf-notification-record-runtime-v1/`.
The unit chain reuses the earlier evidence of the finalizer.

An independent review reported no concrete defect on eleven frozen sources,
140,239 bytes; a second check verified their hashes and the five outputs. The
review is retained under the documented preconditions, without adopting its
verdict of absolute parity. The success oracles notably require an established
menu registry and, if a speed display exists, its complete labels. The tests
were executed during that second check.

No new game trial or forced native call for this batch. The complete queue,
the hosts, the playable menus, the final D3D12 rendering and co-op remain to
be completed. These tests do not measure the smoothness of the complete client.
