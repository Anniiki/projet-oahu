# Numeric conversion of the VMF script reader

## Result and origin of the evidence

`tdur::ui::vmf_number_from_string(string_view)` reconstructs in C++ the
string → float32 conversion used by the actions of the VMF player.
It does not yet constitute an interpreter or a playable interface.

Reference: `0x00442530..0x004426EB`, **four AddressSets, 435 bytes in
a span of 444**. Rea/Ghidra analyzes the secondary SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
The [official trial 25](official_trial_25.md) then corroborates these four
bodies and the constants in the MC 1.66 A copy, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
This presence does not prove an observed original invocation.

Normative source: export 10 `procedure_assembly` of the batch
`rea/vmf-action-interpreter/session/`, evidence
`ev_15f0cce6a2fb910768bae6f1e7a9c76885f3b0d79766fde3ac844098739fc097`.
Resolution 61: `ev_88756a4b05f448cf47381140a0a67a62b1476d960bff30225a557306dbc78362`.
Readings 40/41/42 establish +1, −1 and 10 in float32.
Pseudocode 08 omits the SSE computations; it does not serve as an implementation.

## Preserved rules

- The first NUL terminates the text; otherwise the end of the view bounds it.
- Any **lowercase e** letter, even in a suffix, causes an immediate +0
  return. No mantissa computation precedes this refusal.
- Only an initial sign is consumed. No whitespace is stripped.
- The initial digits form the integer part. The following sequence of
  digits or dots delimits the fraction; only the first dot is skipped.
  Any other dot therefore contributes `'.'−'0'`, that is −2.
- The integer accumulates its contributions from right to left, the fraction
  from left to right. Each multiplication, division and addition stays float32,
  without FMA contraction. The result is `(fraction + integer) * sign`.

This behavior differs from a general decimal parser. Examples in
round-to-nearest:

| Input | Result |
| --- | --- |
| `12tail` / `12E3` | 12, parsing stops at the suffix |
| ` 12` / `12e3` | +0 |
| `-` | −0 |
| `1..2` | about 0.82, bits `3F51EB85` |
| `123456789` | 123456784, bits `4CEB79A2` |
| Forty zeros | NaN: the weight becomes infinite, then `0*inf` is kept |

A view containing `12\0e` yields 12. The bytes are not assumed to be UTF-8.
The helper does not itself truncate strings to 31: that copy belongs to the
stack push and must not be applied to the other operands.

## Compilation and floating-point environment

The `tdur_vmf_number` target uses `/std:c++latest /W4 /WX /fp:strict` under
MSVC. It targets x64/Win32 builds with SSE, modifies neither MXCSR, nor CWx87,
nor the locale, and does not allocate. The input is borrowed only during the call.
The cost depends on its length; the future executor will also have to bound
external strings and its instruction budget.

A first scalar port passed on x64, but MSVC generated an **x87** addition and
multiplication for the Win32 return despite `/fp:strict /arch:SSE2`.
An oracle with directed MXCSR and a distinct CWx87 detected it. The two final
operations now use explicit SSE intrinsics; the ABI return may load an
already rounded float into ST0, without x87 arithmetic.
The test does not artificially synchronize the control words to hide the gap.

## Independent validation

Private batch `vmf-number-tests/`, `validation.json` SHA-256
`df568e19d860087c5a3c479e450d4d07d6bfb7c2bc9e6512f0e462da40a68612`.
The 127 fixed vectors and seven strings in four modes are frozen before the
CPP exists. An exact rational IEEE754 model built on integer arithmetic provides their bits; the test
does not use the port's loop as an oracle. The expectations remain unchanged
between the initial failing Win32 build and the fix.

**4,812 checks pass per architecture**, x64 and Win32: signs, suffixes, the
256 possible bytes, NUL/offset views, rounding thresholds, FMA-distinguishing
cases, directed MXCSR, FTZ, overflows, errno and ordinary C++ allocations.
Guard pages check views without NUL and empty views exactly at the boundary.
RAII scopes restore the control words; naturally raised IEEE flags are
permitted. NaN payload/sign and the order of unmasked traps are not part of
the contract. The initial x87 evidence, objects and disassemblies are kept.

Final sources: header `4486ecfa…dc90b1`, CPP `b6f52ad2…ef8dc1`,
test `899ac94a…29d983`. The parent build integrates `vmf_number_conversion`;
the x64 suite passes 55/55 and the integrated Win32 test passes too. No VMF
script, game callback or controller walkthrough is executed by this validation.
