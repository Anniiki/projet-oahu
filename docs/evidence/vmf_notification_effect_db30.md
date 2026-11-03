# Notification production: 89DB30

`VmfNotificationDB30Context` wires the real text parameters, catalog
formatters, record constructor, queue and C++ text rings.
`VmfNotificationDB30Frame` lets the real child traversal `89DD60` wait for
its completion, then hand back its frame without replaying the operations.

## Official reference

The complete body `89DB30..89DD5B`, 556 bytes, matches between the official
PC MC 1.66 A primary, the secondary and the existing Rea exports. Body SHA-256:
`7889f8c5deb19c394c679b3531b68f1561d06d84707643a45ef35d627e8bc892`.
The primary keeps its identity
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Private evidence: `vmf-effect-db30-runtime-v1/native-proof.json`.
The `%d` and empty-string constants are also cross-checked on disk and
against Rea. Their bodies and commercial resources are not distributed.

The reconstruction is readable C++ with owned objects, without calls to the
original game's addresses or reproduction of its x86 ABI in x64 pointers.
No game trial or capture of its memory in this delivery.

## Preserved order of operations

1. A null global owner finishes before the clock. Otherwise, call the clock
   once and compute the observed DWORD: WORD year modulo 100, DWORDs at
   offsets 2 and 6, then the 12/31 multiplication with 32-bit overflow.
   Do not replace this computation with an assumed calendar. The Windows
   adapter really calls the host system's `GetLocalTime`.
2. Insert four texts through the real `716D30`: weight, evaluator return,
   output argument and limit, formatted as signed 32-bit integers.
   The evaluator receives a valid output address initialized to zero.
   The owner of its second insertion is captured **before** the callback.
   Unknown values wait; they do not become zero.
3. Clear both buffers once. Capture the title owner, write its selector 15,
   then read the high24/low20 ID and run the real `717280` over 128 bytes.
   Then read the body's high2C/low28 ID, reload its owner and format 2048
   bytes, without a new store15. After the two real completions, reload the
   owner and write selector zero.
4. Run the real `88E780` then `8956C0`: fields 00=4, 04=1 and the low word of
   the datepack at 08, title/body, language FFFFFFFF, null descriptor and
   known empty small texts at F035DF.
   The +30 getter supplies the captured object for the real `895AF0`. Errors
   keep the writes, insertions and callbacks already performed.
5. Capture the ring owner 10E7528, then call the real `89BC20` if that owner
   is non-null. Finally reload the parameter owner and call `716CF0`. Its body
   does not guard against a null owner: that case produces a modern error
   after the prefixes, with no false success.

## Ownership and resumptions

The metadata references the same child and stays alive: the IDs are not
copied at the start of the effect. `VmfNotificationNewsOwner` owns the queue
and exposes the same 64 records to the named resolver through
`VmfNotificationNewsHost::news()`. It does not convert a view into a queue.
The facets of the global 114C15C must designate one and the same logical owner.

Contexts are neither copyable nor movable. Formatters retain their buffers
and arguments during waits; each invocation borrows its formatter
exclusively. Keep entries, metadata, owners, sources and services alive and
stable until hand-back. Serialize accesses; these guards do not make the
pools thread-safe.

`VmfNotificationFormatter::cancel()` is a modern abandonment operation: it
releases references, destinations and recursive frames, without rollback.
It refuses reentry during a callback. A refused `begin` grants no right to
cancel another caller's formatter.

A test reproduced a defect of the existing parent: after cancelling a
catalog waiting for an argument, the formatter kept a reference to its local
buffer.
`VmfNotificationDisplayContext` now returns its borrow on fail/dtor.
The same parent/formatter pair can then publish a new notification.

## Validation

**53 suites pass in each architecture, x64 and Win32**, MSVC Release
C++latest, `/W4 /WX /permissive- /utf-8`. This test's own 2187 checks
remain active under `NDEBUG`: real chain down to the rings, 1/0 budgets,
captures after callbacks, live IDs, hook and clear errors, cancellation of two
recursive frames, real 89DD60→89DB30 composition and republication by the
parent.
The 74 tracked sources and six sensitive files are preserved.

The probes of the complete prepared paths observe no call to
`operator new`. Dependencies may allocate in other paths; this result does
not guarantee the absence of stuttering in the full game.
Getters, evaluators, languages, hooks and tables of the tests are declared
fixtures; the Windows clock and the other executed helpers are real.

```powershell
cmake --build build --config Release --target vmf_notification_effect_db30_test
ctest --test-dir build -C Release -R '^vmf_notification_effect_db30$' --output-on-failure
```

The [DB lookups](vmf_notification_database.md) are integrated separately.
The [BF80 effect](vmf_notification_effect_bf80.md) also has its concrete frame.
The [common pool](vmf_notification_effect_pool.md) routes the three frames.
Full hosts, commercial menus and two-machine co-op remain to be connected.
