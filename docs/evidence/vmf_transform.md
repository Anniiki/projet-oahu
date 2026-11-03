# Composition of VMF interface transforms

## Role and origin

`VmfTransform` carries the local and composed transforms used by display
nodes and their instances. It prepares the computation of menu positions and
colors; this module alone builds no instances and draws no interface. PC and
Xbox resources keep their own values.

The contracts come from the **secondary** binary, SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`:

| Routine | Contract | Body bytes |
| --- | --- | ---: |
| `442E20` | Reset to identity | 83 |
| `442E80` | Parent/local composition and flags | 283 |
| `4429F0` | Composition of the eight color WORDs | 200 |

Rea resolved the entry points before the assembly listings were read. The
bytes of each body match the PE file and a bounded DUMPBIN disassembly;
`F4165C` does contain the bits `3F800000`. Private evidence:
`reprise-2026-10-31/rea/vmf-display-lifecycle/session/`, operations 37/38,
53/54, 55/56, 69. No official invocation, Xbox code equivalence or visual
validation is inferred from this static analysis.

## Values and operations

The structure consists of 32-bit flags, eight 16-bit color words and six
32-bit matrix values, with no x86 pointer. The matrix order is `(a,d,b,c,tx,ty)` with
`x'=a*x+c*y+tx` and `y'=b*x+d*y+ty`. Clearing the flags goes with the
identity matrix and with color multipliers of 256, offsets of 0.

The composition always computes the fields, even when the flags are zero. It
keeps the order of the SSE multiplications and additions, each intermediate
result being rounded to binary32; no FMA contraction. The caller's MXCSR
governs rounding, DAZ/FTZ, masks and exception flags.

Colors are signed 16-bit for the computation: signed 32-bit product, division
by 256 rounded toward minus infinity, write modulo 65536. For offsets, the
product of the local offset by the parent multiplier is followed by the
addition of the parent offset. No clamping to a color range is added.

**The output flags depend on their previous value.** For each of the masks
`1` and `2`, presence in either operand forces the bit; otherwise the native
code applies XOR, so it toggles the bit. The other bits survive. The function
explicitly takes `previous_flags`; replacing this behavior with a union or a
reset would be a behavior change.

## Integration and limits

`447E30` composes the parent with the node, except for a child of virtual
category 0: it then goes through `448940` and copies the composed transform
of that child. `447E80` then propagates the transforms to the descendants.
These traversals, the replacements and the instance cache remain to be
connected.

The API returns an independent value and does not reproduce the native code's
writes through an aliased output. It does not allocate and accepts non-finite
bits; checking geometric limits belongs to the future consumer. Validating
the computation does not validate D3D12 or menu rendering.

## Validation

The independent oracle was frozen before the CPP: 159 matrix cases give 636
expectations for the four rounding modes, with exact rational computation and
rounding after each operation. 68 cases cover signed colors and modulo, and
706 combinations cover the flags, including their previous value. The harness
is unchanged. The fixture was only reformatted after its freeze: its 25 793
tokens stay identical, and the initial version is kept. Both architectures
were rebuilt and retested after this step.

The `vmf_transform` test passes 14 250 checks in Release **x64 and Win32**,
C++latest, `/W4 /WX /fp:strict`. It checks in particular the absence of
ordinary allocation, immutability, complete reset and MXCSR preservation. The
rational expectations use DAZ/FTZ disabled; two separate runs check the
processor's DAZ/FTZ policies. Three NaN payloads are recorded as observations
of our helper, without being presented as a native execution.

The integrated suite passes 62/62 x64 tests. Private evidence and freezes are
in `reprise-2026-10-31/vmf-transform-tests/`. The 39-film corpus remains a data
inventory: it is not presented as 39 interfaces executed or displayed.

## Regressions found by the independent review

The first port passed the rational oracle, but a second probe using SSE
primitives with locked operands found 224 NaN payload differences in 112 of
the 131 072 Win32 compositions. No finite or MXCSR difference was present.
MSVC had swapped two operands despite `/fp:strict`. Small explicit Win32
primitives fix this case, without rewriting the resulting NaN and without
modifying the caller's MXCSR.

A separate test then showed an operation permutation on x64 when invalid and
overflow are both unmasked: the first trap was INVALID, whereas the native
order leads to OVERFLOW. Both primitives are now in a separate C++ unit, with
IPO disabled, to preserve the call order. The x64 client still uses the C++
intrinsics.

The two unchanged probes pass after the fix, on both architectures. The
permanent tests `vmf_transform_order` and `vmf_transform_trap` keep these
checks; the reference assembly belongs only to the tests. The faulty versions,
listings and red results remain in the private batch `vmf-transform-review/`.
This check does not execute the commercial binary.
