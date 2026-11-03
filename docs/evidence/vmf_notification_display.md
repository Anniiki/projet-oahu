# Publication of formatted texts: 717FF0

`VmfNotificationDisplayContext` connects the recursive formatter, the
`717420` cache and the VMF setters `440B00`/`440AB0`. It preserves the native
guards, the live references and the effects that precede an error. The tests
use real VMF parsers, dictionaries and runtimes with synthetic data; they do
not yet validate the commercial menus displayed in the game.

## Evidence

Primary reference: PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The complete 298-byte body, `717FF0..718119`, is cross-checked against the Rea
responses for the secondary and the primary on disk. SHA-256:
`2c35c20c341f2ab277ad723a3e1c07ca56d558d85cdee97ce880ee47d81c5efb`.
The constants `(ERR:1)` at `F39FF0` and `(ERR:4)` at `F39F50`, eight bytes each
including their NUL, are also reread on the primary. Private evidence:
`vmf-format-display-runtime-v1/native-proof.json` and `data-proof.json`.
No new native or in-game invocation is performed for this port.

## Branches and order of effects

A null format terminates before any read of the name or any service request. A
name longer than 64 bytes also terminates without effect; 64 bytes are accepted.
The guard is not repeated on a resumption. The metadata of the call arguments
is fixed, while the bytes of the name and of the texts remain live.

After these guards, the context calls `begin_plain` once, then advances the
formatter by phases. This path shares the pre-scan, the collection up to the
first null argument and the replacement, without a second scan.

| Formatter result | Publication |
| --- | --- |
| Order mismatch at pre-scan | Real owned text setter with `(ERR:1)`, without cache or `717BF0`. |
| Marker met before the output limit | Real `717420` cache, then its host publication. |
| No marker met before the limit | Real link to the reference of the **original format**, without touching the cache. |

The last case preserves the whole text, even beyond the 512 local bytes. It
can also preserve a marker located after the output limit: the pre-scan will
still have consumed the arguments it requests. The AL register means "marker
met", not "replacement succeeded" or "no marker anywhere in the source".

The cache branch passes the original name to the existing helper. That helper
can store only 63 useful bytes; the direct branch preserves the 64 accepted
bytes. The cache copies precede the setter. A publication error keeps those
copies and makes the invocation terminal, without rollback or replay.

## Waits and full cache

Unknown formatter and cache bindings wait only when they are needed. A binding
confirmed null is refused on its active path. A selected pool is kept during
formatting; its root call must stay reserved for this invocation until it ends.
The original format is a retained `VmfTextReference`, passed to the formatter
with its complete physical extent, then kept by the link setter.

The full-cache fallback is required only if the helper actually selects that
branch. Without a fallback binding, its modern error `invalid_fallback` occurs
before any copy or host call. The context captures this selection, waits for
the reference, then runs the remaining owned setter. It does not restart the
lookup, even if the cache changed in the meantime. An invalid reference already
supplied is a fault, not an invented fallback value.

A successful setter can be a native no-op. A target that cannot be found
receives neither a variable nor a cleanup flag. An existing target without a
variable name, or a full table, can arm flag 200 without insertion. The
context counters attest the calls, not variables actually written.

Keep owners, display, views, arguments and services alive and stable;
serialise accesses. The context is not movable. Its reentry guard refuses a
nested call on itself without corrupting the outer call. Invalid null pointers
are modern refusals on the paths used, without claiming to reproduce the
effects that precede an undefined native crash.

## Verification

The [catalogue relay](vmf_catalog_display_deferred.md) extends this context
with a constructor for a present format whose ownership remains to be bound.
The new `format_binding` phase takes place after the two guards and captures a
single compatible physical reference, without replaying the name or the
formatter. The usual constructor keeps its null-format meaning. An empty view
without an address remains a known text, with explicit ownership. The
following statistics describe the initial delivery; the relay checks are added
to the same executable.

**45 targeted suites pass on each of the x64 and Win32 architectures**, MSVC
Release C++latest, `/W4 /WX /permissive- /utf-8`. The new program performs 316
checks with the PC 2.8 and Xbox 2.5 profiles, including the 64-byte and 65-byte
names, the 600-byte links, the marker beyond capacity, the reuse of the numeric
scratch, the host errors that preserve the cache and its resumption after
saturation. The 30 tracked sources and six sensitive files remain unchanged
during validation. The measured cache path performs no ordinary allocation; the
dependencies remain outside a general guarantee.

```powershell
cmake --build build --config Release --target vmf_notification_display_test
ctest --test-dir build -C Release -R '^vmf_notification_display$' --output-on-failure
```

The first test with a 64-byte name used a `./` path that the native resolver
does not recognise as an identity. The fixture was corrected to observe the real
name limit of the dictionary; production was not changed to force this test. The
complete host bodies, playable menus, final native rendering and multiplayer
features remain to be connected and verified in the real client.

## Handing back a pending formatter

The [89DB30](vmf_notification_effect_db30.md) batch adds the explicit
abandonment of the borrowed formatter. The display context cancels it on
fail/dtor as long as its own formatting is active; a refused begin does not
cancel a foreign caller. After the real end, the formatter is available
normally. A test reproduced the borrow still held after the parent was
cancelled, then checks that it is handed back and that a new publication goes
through the real catalogue/cache/setter. The prefixes remain acquired; effects
already published are not reset.
