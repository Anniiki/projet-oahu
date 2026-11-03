# Notification toggle: 8986B0

`VmfNotificationToggleContext` reconstructs the sub-service that `89E060`
calls with zero when the level changes from zero to another value.
It writes one owner byte, then publishes a boolean as text in the
active menu. The business meaning of field FA remains open.

## Official evidence

Primary reference: PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The complete 74-byte body, `8986B0..8986F9`, is cross-checked against the
primary, the secondary and the Rea exports. SHA-256:
`64526a37a80655bc95275d5fab2512435b10c0ab7cc6d769a7e83fb73a0bd507`.
The NUL-terminated key at `F0A8F8`, fifteen bytes, also matches. Its text
must be supplied by the installation loader.

The twelve-request Rea session is exported and closed. It also establishes
240 bytes of [89BF80](vmf_notification_effect_bf80.md) and 556 of
[89DB30](vmf_notification_effect_db30.md), integrated in later batches.
The evidence for the 870 bytes alone did not constitute a delivery of the
three services; their C++ validations are documented separately.
Private evidence: `vmf-remaining-effects-runtime-v1/native-proof.json` and
`vmf-toggle-runtime-v1/native-proof.json`. No game launch, native call or
read of game memory was performed.

## Preserved order

The service first stores the **original low byte** of the DWORD argument in
the logical field FA. For example, `0x180` writes `0x80` there, and `0x100`
writes zero. It does not normalize this field to 0/1.

It then calls the real selector `99A780`, group 1 and ordinal 0. A missing
movie, one of the wrong type or one without a display terminates after this
store, without requesting a variable key or calling the setter. A first
selected movie of the wrong type does not trigger a search for another
usable movie.

With a valid display, the real `440BA0` publishes `byte != 0` as decimal
text `"0"` or `"1"`. This normalization concerns the setter only. It uses
the captured argument, even if field FA is modified during a modern wait.
The native return register is not presented as a boolean result of the
operation.

## Resumptions and lifetime

An unknown movie registry waits after the FA store; a known empty view
terminates normally. After selection, display and name are captured. A wait
replays neither the store nor the lookup. Errors from the real setter keep
the prefix and make the invocation terminal.

`VmfNotificationToggleFrame` provides a concrete frame for the
[parent service](vmf_notification_parent.md): it runs this context,
refuses a second active borrower and
keeps the typed error after hand-back. A new invocation clears this
diagnostic. Abandonment does not restore values already written.

Keep the owner, inputs, display, strings and frame alive and stable until
the borrows end. Serialize accesses: these guards are not mutexes. The
classes allocate no ordinary dynamic memory during begin/run/release; the
setter and the other dependencies may allocate.

## Verification

**51 targeted suites pass in each architecture, x64 and Win32**, MSVC
Release C++latest, `/W4 /WX /permissive- /utf-8`. The 154 checks specific
to this service remain active with `NDEBUG`. They use real VMF programs,
runtimes, dictionaries and setters on synthetic PC 2.8 and Xbox 2.5 data:
non-normalized bytes, movie guard, captures, zero budgets, frame reuse and
errors after the display is closed.
The 66 tracked sources and six sensitive files are preserved.

```powershell
cmake --build build --config Release --target vmf_notification_toggle_test
ctest --test-dir build -C Release -R '^vmf_notification_toggle$' --output-on-failure
```

The parent is now wired to this frame through a concrete adapter.
The other effects, the final hosts, the playable commercial menus and
two-machine co-op remain to be wired and verified.
