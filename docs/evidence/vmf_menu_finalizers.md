# Menu data finalization

## Origin and scope

The port completes the row phase of `9AB950`: cache, units and `9AB830`.
The eight routines and 1,188 bytes are [cross-checked in the official process](official_menu_finalizers_runtime.md)
as well as in both PE images. The C++ uses owned objects and callback
identities adapted to x64; it calls no original code.
Fields without an established role remain raw parameters or opaque bytes.
The [shared registry](vmf_menu_registry.md) provides the same selection to
global commands and displays; its lookup keeps the native fallback.

Finishing this data does not acknowledge `sbdd`: receiver, specific services,
tutorial/help, suspended script, input and rendering remain to be wired.

## Order of effects

On an empty cache, `9AB950` copies the source language and then calls `898890`.
That routine stores the mode, runs `6E8A10` if its object exists, then looks up
the active menu of group 1. Two distinct lookups bracket the guards and the
decision to write `/:state_Metric`: zero only for mode 1, one otherwise.
A miss on the first lookup is a no-op; on the second it becomes an explicit
modern refusal here, because the native code then passes NULL to the setter.

`6E8A10` removes the first callback A, clears 3263, removes the first A again,
then the first B, and clears 3262. `99CC40` compacts the records and cleans
only the first byte and the callback of the last slot.
The bank of six words is selected by mode 0/non-zero; the `/:SpeedUnit` text,
copied into that object's display, is selected by mode 1/others. The five
floating-point values are copied as bits, with no computation or conversion.

`9AB830` then calls `898B40`. A cache that is still empty is copied again and
reruns the units: an empty source therefore causes two passes. The language is
resolved through `9A96D0`, the first entry of the five-ID64 table and the first
catalog text. `/:WhatLanguage` is written as a link into the supplied menu,
distinct from the active menu and from the SpeedUnit display.

Language codes compare three bytes including NUL:
us/gb/unknown 0, fr 1, it 2, ge 3, sp 4, ja 5, ko 6, ch 7, ru 8, po 9.
Units compare only the first two: `usx` keeps mode 1 but does not
constitute the `us` language code. The ge/sp and ch/ru indices are
verified against the real constants; the earlier inverted summaries are corrected.

## Objects and continuation

`VmfMenuFinalizerContext::after_rows` consumes a finished row context.
A precondition refusal leaves it resumable; after success it can no longer
produce a second finalization. A moved-from context is invalidated.
Budgets suspend between phases without replaying completed effects.
An error keeps the prefix and forbids resuming that faulted context.

Inputs distinguish an unknown service, a confirmed-absent object and a
confirmed-empty registry. Missing dependencies suspend explicitly; no absence
is inferred from the file's zero fill. Game/file/callback identities stay
stable during a pass, accesses are serialised and linked objects must stay
alive. Host writes keep their native no-ops and their modern errors.
Write counters count calls, not successful insertions.

The ID/language table is read as 80 bytes from the official copy identified
by SHA; no commercial ID or game text is embedded in the module.
The limits of 64 locale bytes, 32 callbacks and 100,000 movies are modern.
Link quotas apply to retained backings, not to a global memory peak.

## Validation

Eight suites pass per architecture, including 853 x64 finalization checks
and 851 Win32 ones, fixtures included. They verify order, double initialization,
ID64/alias, duplicate removal, partial clearing, floating-point bits, mode 2,
second lookup, consumption, missing services and resumption without replay.
The verified run paths do not allocate on the ordinary heap.

The additional diagnostic performs 359 checks per architecture on the
1,119 official rows and the real language table, in a synthetic C++ scene
where the absence of the game service is declared as a precondition.

    cmake --build build --config Release --target vmf_get_variable_test
    ctest --test-dir build -C Release -R '^vmf_menu_finalizers$' --output-on-failure
    .\build\Release\vmf_get_variable_test.exe --menu-finalizers-official "<installation>"

These tests do not demonstrate playable commercial menus or co-op.
