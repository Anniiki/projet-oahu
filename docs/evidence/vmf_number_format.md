# VMF script number formatting

## C++ port and scope

`tdur::ui::vmf_number_to_string(float, precision)` reconstructs the
float32 → text conversion of the VMF player. The result owns a 32-byte
buffer, NUL-terminated; copies and moves borrow no external text.
Views are only accessible on a retained value.

This helper prepares the computed names and the text operations of the
scripts. The helper does not execute those scripts itself. The
[C++ text pop](vmf_stack.md) separately reproduces the aliasing of the
native shared buffer: two owned results do not by themselves prove the
fidelity of that alias.

## Evidence and limits

Secondary PC reference: entry `0x00442330`, last observed instruction
`0x00442520`. Binary SHA-256:
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
Normative source: export 86 `procedure_assembly` of the private batch
`rea/vmf-action-interpreter/session/`, evidence
`ev_0d7705644be9457fc8e35b84a1a9158fd1b2f9c8690f4dc6f26ca567340dda4e`.
The pseudocode export 35 helps locate the calls; the port keeps the order
of the SSE operations of the assembly.

The static check of the secondary confirms 18 constant bytes:
`F4165C=1`, `F4166C=10`, `F41674=0.5`,
`F4167C=0x3A83126F` and `F0D0A0=".\0"`.
This formatter was not part of [official trial 25](official_trial_25.md).
Its presence or its invocation on the official copy is therefore not
corroborated by that trial. This batch executes no commercial code.

## Preserved rules

- Positive precision is capped at 4; the native text pop requests 12.
- The sign depends on the ordered comparison `0 > value`. Thus −0 yields
  `0`, and DAZ can remove the sign of a negative subnormal.
- The integer is obtained with CVTTSS2SI, then converted back to float32.
  The fraction receives the `fraction + 0.001 >= 1` treatment before it is
  scaled.
- The fraction is multiplied by 10 once per decimal place, then increased by 0.5
  and truncated. Its trailing zeros reduce the width; no additional decimal
  carry is propagated to the integer.
- NaN and overflows use the SSE sentinel INT32_MIN, without undefined C++
  conversion. The operations follow MXCSR, independently of CWx87.

Examples under round-to-nearest: `-12.375 → -12.375`,
`0.75, precision 0 → 0.1`, `1.995f, precision 2 → 1.1`,
`−inf → --2147483648`. These peculiarities are preserved for the scripts.
A negative precision explicitly returns `negative_precision` before any
computation: the unbounded native code can overflow its padding counter.

## Validation

Private batch `vmf-number-format-v1/`: 414 vectors, that is **1656 expected
texts** across four MXCSR modes. Rational IEEE754 model without host
floating-point arithmetic; decimal placement independent of the C++ loops.
The values and the harness are frozen before the C++ code. JSON oracle
SHA-256:
`624fb4aa2fb8c6d2c3004db2ca2964a3a772f0e96323ceed27e4fe21cb9fc25d`.

**15073 checks pass per architecture**, x64 and Win32: neighboring
thresholds, precisions 0/1/2/4/12/INT_MAX, signs, extreme values, NaN,
FTZ/DAZ, negative-precision refusals, text ownership, errno and the
absence of ordinary C++ allocation. The test does not synchronize CWx87
and MXCSR to mask a discrepancy. Floating-point controls are restored;
naturally raised IEEE flags are permitted. The order of unmasked traps is
not contractual.

The target remains `tdur_vmf_number`, with `/fp:strict` and explicit SSE
intrinsics; the CTest test is `vmf_number_format`.
