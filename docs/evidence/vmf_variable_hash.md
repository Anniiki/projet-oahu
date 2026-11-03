# VMF variable name hashing

## Reconstructed component

`vmf_variable_name_hash(scope, name, capacity)` rewrites the VMF dictionary
helper in modern C++, independently of the x86 pointer layout. The
`tdur_vmf_variable_hash` target compiles for x64 and Win32. This component
does not yet implement the variable table, the setters, scope removal or the
menu wiring.

## References and official corroboration

Observed source: `00443390..00443401`, 114 contiguous bytes of the secondary,
SHA-256 `f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
Rea/Ghidra, export 96 `procedure_assembly` from
`reprise-2026-10-30/rea/vmf-action-interpreter/session/`, evidence
`ev_f77c4d11ee3f18f8709e937c3b257acac92afbbf9af3081abe66fe5f1bb564c5`.
The C++ is rewritten from the instructions, without pseudocode translation.

The static comparison at the same virtual addresses yields different bytes in
the official PC MC 1.66 A file, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Both PE images map this window at offset 275344. Body fingerprints:

- secondary: `b6042e3b5cb81d63314d687c3bcfd9efd50cce48030f002f5d549bf8957e5349`;
- official on disk: `3d07fa85b44317f0c4a8d0f83db0d742bc5b7698e65252c2fbabb68807bcac34`.

This finding does not determine the cause of the discrepancy. [Official trial 25](official_trial_25.md)
excluded this callee; a separate observation now corroborates it in the
private copy of the official executable, whose SHA-256 remains the one above.
The official file remains the project's primary reference.

The exclusive `ui-vmf-hash` profile of `tdur_probe_code` captured **114 bytes**
at `00443390`, with no constants and no pointer walking. Their SHA-256 is
`b6042e3b5cb81d63314d687c3bcfd9efd50cce48030f002f5d549bf8957e5349`:
they match the secondary but differ from the official file on disk.
The cause remains unknown. The presence of this code does not prove that it is called.

Private trial: `<isolated-copies>/slot0/20261031T054423-28568`, PID 35112,
creation FILETIME 134378990990130686, image at `00400000`. PID 28568 is that
of the Python observer. The limit of the owned Job stopped the game after
100.16 seconds (1460), then released the slot. The observer receipts report
**6,697 reference files and 17 source saves unchanged**.
No menu, keyboard or mouse action was sent.

The probe only reads the named ranges, with no write and no protection
change. This guarantee concerns the probe: the reference context remains
instrumented, x86, D3D9On12 and borderless. It is not the final x64 client
with native D3D12 rendering. `runtime-parent-comparison.json`, in the private
validation folder below, links the twelve artifacts by fingerprint and checks
identity, bytes, audit reports and Job termination without relaunching the game.
A later documentary review corrected the causal inferences of the earlier one;
the three deliverables were then checked and the validation decision is
retained.

## Preserved rules

- The first NUL terminates the name; the end of the view serves as the host bound.
- An empty name returns 0 without consulting the scope or the capacity.
- The scope initializes an unsigned 32-bit accumulator; additions wrap
  modulo 2^32 before the unsigned division by the capacity.
- Groups of four bytes are built explicitly in little-endian order.
  The `DFDFDFDF` mask applies to all bytes, punctuation and non-ASCII included.
- The temporary word is initialized once. Bytes not replaced in a final
  partial group keep those of the previous masked word.

Example: `abcd` yields the word `44434241`. For `abcde`, the second word is
`44434245`, not `00000045`; their sum is `88868486`. With capacity 97,
the result is 60. Names are hashed in full: the
[dictionary](vmf_variables.md) applies its 31-byte storage truncation
after this computation.

The explicit refusal of a zero capacity with a non-empty name replaces the
native division fault. No access beyond the view, allocation, locale or
floating-point environment change is required.

## Validation

`vmf_variable_hash_test` performs **72 checks per architecture**: words and
partial-group retention, scope and overflow, NUL, punctuation, non-ASCII,
zero/unit capacity, 31/32/33-byte keys and the buckets of an independent oracle.
Views without NUL of length 0..12 end against a `PAGE_NOACCESS` page;
both test processes run these cases without reading beyond the view.

MSVC `/std:c++latest /W4 /WX /permissive-`, Release x64 and Win32 builds:
72/72 in each, targeted CTest 1/1 in each. clang-format 19.1.5 formatting,
four spaces and 80-column limit. The private artifacts are in
`reprise-2026-10-31/vmf-variable-hash/`.

These checks establish this bounded helper; they validate neither script
execution by the recompiled game nor multiplayer or co-op.
