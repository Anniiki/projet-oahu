# VMF instance property assignment

## Result and references

`execute_vmf_actions_with_properties` connects `SetProperty` (`23`) to the
display and to the frame player. With the owned dictionary, this entry executes
30 opcodes and preserves six host boundaries. The earlier entries keep their
contracts. `GetProperty` (`22`) remains to be ported.

Secondary PC static reference, SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
The private batches `reprise-2026-10-31/rea/vmf-property-host-v1/` and
`vmf-property-math-v1/` keep the sources and manifests:

- The tables at `443E6C/443DE8` link `23` to `4464A0` and `22` to `445DF0`.
  The first batch compares 5 494 bytes of PE/Rea/DUMPBIN code and 328 bytes of
  PE/Rea tables and constants. Its 2 471 bytes outside recognized AddressSets
  are switch branches. Ghidra still classifies `445F4D` as data.
- The complement closes `442C70`, `448070`, `44B2C0` and the `_CIacos` thunk:
  four bodies, 194 bytes, plus 92 bytes of tables and constants. The import
  `MSVCR71.dll::_CIacos` is cross-checked in the PE at IAT `9EC29C`; its code
  is neither loaded nor analyzed by this port.

These new bodies remain secondary. No new observation of the official client
and no equivalence with the XEX code is inferred from this evidence.

## Preserved order and effects

The handler pops the value as text, the index as a number, then the target.
The index goes through `ADDSS 0.5`, `CVTTSS2SI32`, then an unsigned comparison.
The target is a typed sprite identity or a path from the current context.
A missing target consumes the operands without changing the context or
triggering a skip. The value is read after all the pops: formatting a numeric
target may have rewritten its shared scratch buffer. An early copy would change
this effect.

| Indices | Native effect with logging disabled |
| --- | --- |
| 0, 1 | Position multiplied by 20, override then propagation. |
| 2, 3 | Scale multiplied by 0.01, base recomputed, override then propagation. |
| 6 | `(value × 256) × 0.01`, SSE conversion, low 16 bits of the alpha; no clamp. |
| 7, 16, 17 | First character equal to `'1'`: bits 0, 1, 2; no propagation. |
| 10 | Rotation preserving both norms; sine rounded to float before the products. |
| 13 | No state change. |
| 18 | Numeric conversion preserved, then no state change. |
| Others | No state change. |

The numeric writes 0/1/2/3/6/10 require a parent after conversion. The three
bit writes remain possible on the root. The other bits, including the cleanup
flag `200`, are preserved. Stale identities and unbounded accesses are refused;
the pop prefixes already performed are not undone.

## Computations and numeric limits

`442C70` computes an acos from a ratio that uses a norm rounded to float, then
multiplies by 57.295776f. The scale helpers pass this result in degrees
directly to their trigonometric functions: this observed peculiarity is
preserved. The rotation follows its own factor `3C8EFA35`.

The C++ uses the double functions of the current CRT, without emulating all of
x87 or MSVCR71. An owned arithmetic reference executes FCOS/FSIN in Win32,
PC53/RNE, with the same modern acos: the 720 float words of the 180 cases are
identical in x64 and Win32. The corpus also covers the unchanged pass-through
of very large finite arguments, observed with x87. Reference and production
share the same author; neither the old CRT nor every floating-point mode is
validated.

## Usage and validation

Enable `VmfFramePlayerLimits::properties`. This service uses the dictionary
already bound, without enabling it implicitly. The diagnostic flag is
independent:

```powershell
.\build\Release\tdur_inspect_vmf_playback.exe movie.vmf pc --text-bindings --properties
```

The 21 targeted suites pass in both architectures: 1 556 property checks and
240 operations without ordinary allocation per architecture. The tests cover
target graphs, aliases, flags without propagation, identities, non-finite
indices, alpha, parent guards, budgets and execution by the player. An earlier
review supplied conditional flag expectations; the missing graphs and canaries
were checked separately. A second bounded static review
offers no guarantee of full conformity.

The 468 diagnostics on 39 movies agree between architectures; the 390 earlier
results are preserved. Four PC movies complete the 16 bounded invocations,
including `GENERAL` with 15 variables and 11 texts; 14 reach `83` and one
`9F`. The 20 donor movies still refuse the PC text backend. Inputs and the two
commercial executables keep their SHA. No commercial game is launched; no
displayed menu or co-op mode is validated.
