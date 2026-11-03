# Name hashing: 504930, 42E1E0

The `resource_name_hash` module reconstructs the name-hashing choice
used by notifications. It reuses the
[existing resource backend](resource_hash.md); its callers and their
connection to the game's actual mode remain to be integrated.

## Two distinct modes

`hash_resource_name(name, mode)` ports wrapper 504930. It considers the
prefix before the first NUL. A bounded view's end implies a NUL; bytes
must remain readable and stable throughout the call.

| Supplied mode | Processing | Empty name |
| --- | --- | --- |
| 0 | Shared, case-sensitive 504980 backend | Seed `0x0CD5E44AFAC432B1` |
| Any nonzero byte | Historical helper 42E1E0 | Zero |

Mode is supplied explicitly by value, like reading the native global
byte before `strlen`. Values 2 and 255 also select historical mode.
Observing FAC598 at 1 during a trial does not establish a permanent
default for the port.

`hash_legacy_resource_bytes(input, count)` exposes the historical helper.
A zero or negative signed count returns zero without reading the buffer.
Otherwise, every ASCII byte from `a` to `z` becomes uppercase; other
bytes, including those above 127, remain raw. Eight byte accumulators
receive values successively, with modulo-256 addition and no carry
between accumulators. The result packs bytes 0 through 3 into the native
low word EAX, then 4 through 7 into EDX.

The raw helper consumes exactly the requested count, including NUL;
the name wrapper stops at the first NUL. No slash, locale or Unicode
normalization is added. A positive span that is too short gives
`count_exceeds_input`; a name length exceeding the signed count gives
`input_exceeds_signed_count`, without truncating conversion.

## Evidence and validation on 01/11/2026

Both complete bodies, **156 bytes**, are corroborated in the official
MC1.66A process by the [targeted trial](official_notification_helpers.md).
The wrapper has two ranges of 42 and 23 bytes, separated by six excluded
alignment bytes; the historical helper is 91 bytes. Backend 504980
retains its earlier memory evidence and verified table; it is presented
as neither a new port nor a new read.

MSVC Release C++latest, `/W4 /WX /permissive- /utf-8`: **29 suites pass
on x64 and Win32**, including 27 new checks and the backend's existing
26 370 checks. Fixed vectors are cross-checked against native register
traces and table: case, high bytes, accumulation over multiple rounds,
interior NUL, modes 0/1/2/255 and short spans. Shared-backend tests do
not provide a new independent oracle for the wrapper.

```powershell
cmake --build build --config Release --target resource_name_hash_test resource_hash_test
ctest --test-dir build -C Release -R '^native_resource_(name|key)_hash$' --output-on-failure
```

Six frozen sources, six preserved sensitive files and evidence are
recorded outside Git in
`<private-evidence>/reprise-2026-10-31/resource-name-hash-runtime-v1/`.
The C++ representation does not impose the x86 ABI. This batch validates
neither a targeted native invocation, a playable commercial interface,
co-op nor overall smoothness.
