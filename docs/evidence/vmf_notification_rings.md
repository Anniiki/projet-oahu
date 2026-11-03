# Notification text rings: 89BBC0, 89BC20

The two C++ functions own the texts of eight slots and advance their index
after a successful write. The connection to the host and to the game callers
is still required; this batch does not display its notifications yet.

## Order of writes

`push_vmf_notification_text_single` carries 89BBC0. The routine copies at most
400 bytes, pads a short string with zeros, forces the NUL at index 399, then
advances the index modulo eight. It returns the previous index multiplied by
800, like the native EAX: a zero return can therefore indicate success.

`push_vmf_notification_text_double` carries 89BC20. It copies the first
400-byte text, then the second, **before** forcing the two final NULs and
advancing the index. The second source is read after the first copy: a view
over the first destination receives its new content. Crossed sources follow
this order; they do not perform a simultaneous swap. The native return sets
AL to zero without guaranteeing that the whole of EAX is zero.

High bytes stay raw; no UTF-8 conversion is added. An internal NUL ends the
copy and the rest of the buffer receives zeros.

## Storage and modern errors

The structures own only the logical contents, without imposing the x86 ABI.
The native single ring has a stride of 800 bytes but writes only 400 of them.
The interval following its last text holds the 320C counter: it is not an
opaque, independent 400-byte tail per slot. The C++ zero initialization
does not reconstruct the host constructor.

Accesses must be serialized and the buffers must stay valid and stable. A
`string_view` provides a bounded prefix of a C string; its end implies a NUL.
Its content is read at the corresponding phase. A mutable view over a
destination must expose the whole readable window required, for example
400 bytes for the second source bound to the first destination.

Indices outside 0 to 7 are refused before any copy. A missing source fails at
its phase; the earlier copies are retained. Thus, if the second source is
missing after a first copy of 400 `A` characters, the last `A` stays present:
the final NULs and the advance are omitted. Disjoint copies, or copies exactly
onto themselves, are supported; other overlaps produce an explicit error. No
implicit replay.

## Evidence and validation on 01/11/2026

The two complete bodies, **217 bytes**, match the official MC1.66A binary on
disk, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The Rea/PE/DUMPBIN evidence and its files are verified again.

MSVC Release C++latest, `/W4 /WX /permissive- /utf-8`: **27 suites pass on
x64 and Win32**, including 31 targeted checks. They cover each index, the
wrap-around, the truncation, the padding, the preservation of the other
slots, the live aliases, the null sources and the errors after a partial
write. Four frozen sources and six sensitive files remain unchanged during
the validation, recorded outside Git in
`<private-evidence>/reprise-2026-10-31/vmf-notification-rings-runtime-v1/`.

```powershell
cmake --build build --config Release --target vmf_notification_rings_test
ctest --test-dir build -C Release -R '^vmf_notification_(rings|text|queue|record|reset)$' --output-on-failure
```

No new game trial and no forced native call for this batch. The playable
commercial menus, the final native rendering, the online profile, the co-op
and the smoothness of the complete client remain to be verified separately.
