# Classification of VMF variable references

## Reconstructed component

`tdur_vmf_variable_reference` provides
`classify_vmf_variable_reference(input, max_bytes)`. It reconstructs the
classification of the inputs of `4491A0`: local name, name attached to a
target, or target alone. It does not resolve the target, does not access the
dictionary, does not modify the stack and does not yet implement
GetVariable/SetVariable.

The returned views borrow the input: the input must stay alive and unchanged
until the following services consume them. No native x86 pointer is exposed
as a C++ object identity.

## Observed rules

| Input | Target lookup | Variable name |
| --- | --- | --- |
| `A` | None, current target | `A` |
| empty string | None, current target | Present, empty |
| `A:B` | Whole path `A:B` | `B` |
| `:` | Whole path `:` | Present, empty |
| `A/B` | Whole path `A/B` | Absent (native `NULL`) |
| `A/B:C` | Whole path `A/B:C` | `C` |
| `A:B/C` | Whole path `A:B/C` | `B/C` |

The first NUL terminates the text. The search for the first `:` precedes the
search for `/`: the case is not decided by whichever delimiter comes
first. Without `:`, a `/` selects a target alone.

The path handed to the resolver stays **whole**. It must not be cut before
`:` or `/`: the resolver may compare whole stored names before processing
their delimiters. An empty name remains distinct from an absent name, notably
for the future sprite push by the `449070` wrapper.

## Sources and documentation correction

Analyzed secondary executable: SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
Private exports `reprise-2026-10-31/rea/vmf-text-services/session/`:

- `12-procedure_assembly.json`, classifier `4491A0..449230`, 145 bytes,
  evidence `ev_d28e107828510579d7075e3491a5a763d55542e7f18c26ba4a821c61cb817c40`;
- `14-procedure_assembly.json`, resolver `4490E0..449197`, 184 bytes,
  evidence `ev_ea7e9d2053932f45691c4f94583429b4620633c2e8c9afbcc74c0ef4d9b25a21`.

The secondary PE identifies the import `009EC348` as `MSVCR71.dll!strchr`.
The import name is checked from the PE thunks, without assuming a search for
the last delimiter. The instructions `4491B6..4491C4`, `4491C6..4491F5`
and `4491FE` establish the rules above.

These two functions **are not font/localization services**: earlier notes
had misclassified them. The C++ wiring hints from the earlier integration
index are accepted for their current sites only; their missing native
references close no text or focus service.

## Corroboration in the official executable

The official PC MC 1.66 A executable carries SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Exclusive profile `ui-vmf-variable-references`: **two ranges, 329 bytes**,
matching the two Rea/PE/DUMPBIN references of the previous text batch.
Ordered sum:
`26499ae44e80182cc340ae0f70a254ab31481d8b52780511e9031f5a3029548c`.

Private batch `<isolated-copies>/slot0/20261031T065728-10592`, target PID 19608,
creation FILETIME 134379034551153645, image 00400000; Python observer PID 10592.
The executed private copy remains `slot0/20261030T035944-25460/game/` with the
official SHA, verified and locked. After 100.155 seconds, the Job reaches its
expected timeout (code 1460) and the slot is released. The observer's audit
receipts report **6697 references and 17 source saves unchanged**.

The probe is read-only, with no constant, pointer walk or free address. The
context remains instrumented x86, with the requested D3D9On12/borderless
display; no UI input was sent. Presence of the code does not mean an
attested call, a native x64/D3D12 final client or a playable menu.

The private parent verifier `vmf-variable-reference-v1/verify-runtime.py`
links the identities, captures, probe archives and audits. The launch receipt
contains a 1479-byte auxiliary field inherited from the previous batch:
`runtime-receipt-addendum.json` explicitly excludes it from this evidence. Only
the current catalog, its two `secondary_references` and the probe identity are
used. The history is not rewritten.

## Guards and validation

The end of the view serves as a logical NUL, with no read beyond the view. The
modern quota counts the whole prefix, including the variable suffix; a refusal
returns `text_limit`. It depends on no locale and allocates no ordinary C++
memory. Non-ASCII bytes remain opaque.

**215 checks pass per architecture**, Release x64 and Win32: sixteen
independent cases frozen before production and tests, quotas, NUL,
absent/empty name, preservation of the whole path and views without NUL
against `PAGE_NOACCESS`. Source oracle SHA-256
`2171f63e276423d60bcba9d2bd5c3c08e58fb821c1be5a816961df83b55c98dc`.
The sixteenth case describes a host view end; its `input_bytes` are
authoritative.

Build `vmf_variable_reference_test` then run CTest
`-C Release -R '^vmf_variable_reference$' --output-on-failure`.
The probe checks also pass in both architectures. These tests do not yet
validate the complete target resolution or its integration.
