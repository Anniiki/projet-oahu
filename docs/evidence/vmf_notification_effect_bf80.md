# Publishing a notification text: 89BF80

`VmfNotificationEffectBF80Context` composes the three existing data lookups,
text parameters, catalog formatter and single ring.
Its concrete frame can be borrowed by the real child path `89DD60`.

## Official reference

The complete body `89BF80..89C06F`, 240 bytes, is corroborated by existing
evidence from the PC MC1.66A primary, the secondary and Rea. SHA-256 of the
body:
`35544d0b4b0f83204eed219a1e3ca3943250fff0156299f69597d31c861434dc`.
Identity of the primary:
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The private evidence `vmf-effect-bf80-runtime-v1/native-proof.json` references
these already closed exports. No new call to the game and no executable
analysis in this batch; the C++ calls no native game routine.

## Preserved order

A global owner guard precedes the clock, which is called once even if its
result stays unused. The key32 `+38` is then captured. Zero or a missing
record skips the subsequent lookups. The same fixed data object supplies the
record and name facets; the same captured row is passed to the group lookup,
which then reads its live key. A missing name does not become an empty string:
the native code would form the invalid address `0xC`.

The first owner is reloaded before the wait for the inline text and its key.
The group text pointer is captured before the second owner. A NULL group value
reaches the real binder `716D30`, which ignores it even with a NULL owner. The
text keys are supplied by the loader.

The clearing of the 2048 bytes and the store of selector `15` precede the high
`+2C` read, then the low `+28` read, and the wait for the formatter.
Resumptions keep these captures and do not rewrite the selector. After the
formatter has really finished, the owner is reloaded for the `0` store and the
`716CF0` clear; NULL at this point is an explicit error. The display owner is
captured before the real `89BBC0`: its zero-offset return is indeed a success.

## Resumption and lifetime

Contexts and frames are not movable. Metadata, inputs, services, owners and
borrowed storage stay stable and alive; accesses are serialized. Views capture
an address while its contents remain alive. Steps are bounded and the report
distinguishes local and cumulative progress. This is a modern continuation,
with no claim of a native asynchronous ABI.

Cancellation releases only a formatter whose begin succeeded, including
its descendants, before the buffer is destroyed. A refused begin does not
cancel someone else's work. Errors and abandonment keep the earlier effects;
reentrant calls do not destroy the active frame.

## Validation and limits

**55 suites pass in x64 and Win32**, including **5371 BF80 checks** active
under MSVC Release/NDEBUG and C++latest. They cover captures, unknown/NULL
values, terminal errors, budgets, recursive cancellation and reuse. The real
child→dispatch→variable setter→BF80→ring chain is executed, then cancelled
while waiting and reused with the same frames. Two regressions were reproduced
before correction: clock repetition after a fault and store15 delayed by the
waits.

```powershell
cmake --build build --config Release --target vmf_notification_effect_bf80_test
ctest --test-dir build -C Release -R '^vmf_notification_effect_bf80$' --output-on-failure
```

The probes of the prepared path observe zero ordinary allocations; they do not
guarantee the absence of game stuttering. Tables, clock and catalog of these
tests are synthetic. The [common routing module](vmf_notification_effect_pool.md)
is supplied separately. The complete hosts and loaders, the commercial menus
and co-op remain to be connected.
