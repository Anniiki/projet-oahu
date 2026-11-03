# Wrappers and recursive currency

`VmfNotificationFormatter` reconstructs the controls `7171B0` and `717280` and
actually connects the currency path to `717BF0`, `7178C0` and `719AD0`. Nested
calls each hold their own state and resume after a missing dependency. The
host getters and catalog texts used by the tests remain fixtures; this port
does not yet constitute a playable menu or any online feature.

## Evidence and argument boundary

Primary reference: PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The Rea exports of the secondary
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`
are cross-checked against the primary on disk:

| Full body | Bytes | SHA-256 |
| --- | ---: | --- |
| `7171B0` | 204 | `a96377fd93d657be90a3477ba10f1b4ed24dc8a2f193971616d780c5b483f9b3` |
| `717280` | 115 | `81331d8d11057e4b6cd8d82bba7f150ca43343fee10b6c42d3f64ca756c8cd41` |

The `(ERR:1)` fallback at `F39FF0`, NUL included, is also verified over eight
bytes. The private evidence is
`vmf-format-wrapper-runtime-v1/native-proof.json`. No new native call or game
trial is performed for this batch.

The 82-byte helper `416450` remains uncorroborated on the primary. It is not
presented as a ported function. A modern boundary receives eleven slots
**already marshaled and typed** and replaces access to the x86 `va_list`. It
does not reproduce reads of uninitialized words on the stack and converts no
DWORD into an x64 pointer. The money signature supplies one numeric
argument; its other typed slots are explicitly null.

## Guard, catalog and collection

`begin_plain` corresponds to the control `7171B0`. A null format preserves the
destination, even with a zero capacity, without requesting unused arguments or
an unused host. Otherwise the real pre-scan `716F60` runs before any write. Its defined
result lies between 0 and 9, or equals -1 for an ordering mismatch. In that
last case, the wrapper copies `(ERR:1)` with NUL padding, forces the final
byte and finishes without calling the replacement engine. A zero capacity on
this path is a modern refusal.

Collection consumes only the requested prefix, then stops at the first
argument confirmed null. `VmfNotificationCallArgument` distinguishes:

- outer `nullopt`: argument still unknown, therefore a wait;
- outer value containing `nullopt`: confirmed null pointer;
- committed source: borrowed view or retained owned reference.

Collection captures the source of each argument exactly once; its bytes stay
alive during copies. `§1abc§` may require the first argument at pre-scan time,
then follow the named branch of the strict parser `717BF0`.

`begin_catalog` corresponds to the control `717280`. The catalog registry is
captured before the owner's selector. Descriptor computation keeps the uint32
wrap of the existing module. A missing ID becomes a null format and preserves
the buffer. A missing or invalid descriptor remains a distinct error.

## Frames and recursive resumption

`create` preallocates a pool of 1 to 32 frames, 16 by default, root included.
Each frame holds its own steppers, references, collection and currency
adapter. A grandchild's wait resumes that same grandchild; it does not
replace the parent's active invocation with the last global frame.

Global inputs are refreshed before each `resolver.run(1)` phase. The first
`COMPL_PCT` getter can therefore change the host observed by the second, even
within a large parent budget. Named parameters stay bound to the owner of the
invocation. The currency owner is read and its `begin` runs in the same phase;
later global changes do not replace the captured owner.

Exceeding the **configured** depth produces `recursion_limit` while keeping
the earlier writes; no partial result becomes a success. A new invocation can
reuse the pool after this fault. Accesses are serialized: the reentry booleans
do not synchronize threads. Keep services, arguments, owners, registries and
buffers stable and alive during execution. Moving the formatter preserves the
pool storage, without allowing a move during an active call.

## Validation and next steps

**44 targeted suites pass on both the x64 and Win32 architectures**,
MSVC Release C++latest, `/W4 /WX /permissive- /utf-8`. The batch comprises
103 wrapper checks, 238 money checks, 303 named-resolver checks and the
75 earlier checks of the replacement engine. The 27 tracked sources and
six sensitive files are preserved during validation.

The scenarios cover guards, catalog errors, stops on NULL, unknown arguments,
live sources, references retained after destruction of their creator, host
changes, a wait at the third level, reentry and reuse after exceeding the depth limit. The
measured recursive path performs no ordinary allocation during `begin/run`;
dependencies may allocate.

```powershell
cmake --build build --config Release --target vmf_notification_wrapper_test
ctest --test-dir build -C Release -R '^vmf_notification_wrapper$' --output-on-failure
```

The [display port `717FF0`](vmf_notification_display.md) now completes
publication to the setters and the cache. The complete host implementations,
playable menus, final native rendering, online and co-op features remain to be
completed. The sources and targeted tests constitute no proof of universal ABI
conformance or of a working complete game.
