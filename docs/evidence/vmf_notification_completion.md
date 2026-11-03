# Percentage computation: the COMPL_PCT branch of 7178C0

`compute_vmf_notification_completion()` computes the integer value that this
branch then writes with `%d`. The API receives the `uint32` sum of the real
computation `89BD90` and an **explicitly established** x87 control word.
It replaces neither the two preceding host calls nor the text write.

## Order and rounding

The sequence loads the bits of the sum as a signed integer, adds `2^32` for
negative values, multiplies by the float with bits `3A83126F`, then by `100`,
and stores the result as binary32. The helper `40C1A0` widens this float to
double and calls `floor`. The conversion `674EB4` then keeps this integer
value within the positive domain considered.

The C++ reproduces the intermediate roundings with integers, without modifying
the program's floating-point environment. The x87 precisions of 24, 53 and
64 bits and the four rounding modes are supported. Exceptions must be masked;
a reserved PC01 value and unmasked exceptions produce explicit modern
refusals. The intermediate significands fit in 62 bits and the integer result
stays below `2^30`.

Examples from the verification program, for a sum of `10`:

| Explicit precision / rounding | binary32 bits | Integer |
| --- | --- | --- |
| 24 / nearest | `3F800001` | `1` |
| 24 / toward negative infinity | `3F7FFFFF` | `0` |
| 53 / nearest | `3F800000` | `1` |

These control words are test parameters; they do not claim to establish the
one used by the game at this call site. The sum is not divided by ten and no
cap at 100 is added.

## Evidence and validation

The constants and the parent body are corroborated on the official PC MC 1.66 A
primary executable, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The [official trial](official_formatter_imports.md) confirms the two loaded
numeric bodies, 137 bytes, and the `floor` binding, four bytes.
It does not invoke them in a targeted way.

A separate x86 program executes the instruction sequence and calls the
verified CRT: **576 cases**, that is 48 sums across twelve modes. An
independent rational-arithmetic oracle agrees bit for bit. Its announced
result bound is corrected: one observed case yields `429496768`. The generic
numeric vectors are kept with the tests; no game resource is involved.

MSVC Release C++latest, `/W4 /WX /permissive- /utf-8`: **36 targeted suites
pass on each of the x64 and Win32 architectures**, including 670 checks of the
computation: vectors, refusals and preservation of the ambient modes/flags.
Nine frozen sources and six sensitive files remain unchanged during validation.
Private logs:
`reprise-2026-10-31/vmf-notification-completion-runtime-v1/`.

```powershell
cmake --build build --config Release --target vmf_notification_completion_test
ctest --test-dir build -C Release -R '^vmf_notification_completion$' --output-on-failure
```

This port concerns values. It emulates neither the historical x87 flags/status
nor the other possible inputs of `674EB4`. The wiring to the formatter, the
playable menus and the co-op remain to be completed.
