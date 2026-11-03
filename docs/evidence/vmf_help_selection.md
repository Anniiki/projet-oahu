# Help selection and menu lookup

## Official reference

The port covers the decisions of 9812C0, the 32 routing identifiers of
980FB0, the name lookup 99A490 and its composition 99A950.
The four complete AddressSets total 1 192 bytes. The Rea and DUMPBIN reads
are cross-checked against the PE sections of both files:

- Primary PC MC 1.66 A: SHA256
  3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30.
- Secondary: SHA256
  f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed.

The four bodies, the three branch tables (140 bytes) and the functional
strings (284 bytes) are identical in both files on disk.
These comparisons prove neither their execution nor a usable screen.
Private evidence:
`<private-evidence>/reprise-2026-10-31/vmf-common-help-runtime-v1`,
`static-proof.json`; Rea session vmf-common-help-v1, 54 requests.

## Resumable selector

VmfHelpSelectionContext captures the world type and the selected activity
record once. A known absent world and unresolved services are distinct:
a required virtual call remains a typed pending request.
The code does not read the PE zero initializers as live state.

The decisions keep the AL tests, the full EAX tests and the signed comparison.
Mode 3 calls both threshold/blocking services; mode 4 exits.
For a category 5 activity, the second virtual call +1C is distinct from
the first and the 64-bit identity of the record is reread after that call.
The borrowed record must stay alive, at a stable address; its mutations
and service replies are serialized by the owning engine.

A reply of the wrong type or out of order is refused without consumption.
A consumed reply cannot be replayed; a move invalidates the source
context. The adapters for the virtual services remain to be ported.
These replies are not evidence that the native service is available.

980FB0 also keeps its shared "tip_" entries: they do not mean an absent
pointer. -1 and indices >= 32 yield a known absence; the other negative
indices are refused, where the native code would read outside the table.
The functional strings are not localized game texts.

## Menu registry

99A490 uses MSVCR71!_stricmp, identified in the PE imports.
The port offers a modern ASCII domain with a deterministic A-Z fold;
it refuses other bytes and names exceeding the native 32-byte field.
The locale configuration of the native CRT is not proven by this import.
General equivalence with all of its locales is therefore not claimed.

The first admissible name wins. Even a negative context first requires
reading the current context. The ordinal counts every previous
registration of the same context, or every previous registration with a
negative context, without filtering their names. 99A950 forwards
group/ordinal to the helper [99A780](vmf_menu_finalizers.md), then requires
kind1 to obtain the display.
The helper's last match may be retained when the ordinal is too large.

## Validation and next steps

The vmf_help_services test covers 1 983 checks in each architecture:
256 category/sub-category values, guards, services, identity64,
resume/move, routing and synthetic registries. The menu data and
finalization suites remain verified separately.

    ctest --test-dir build -C Release -R '^vmf_help_services$' --output-on-failure

The [TDU_Tutorials](vmf_tutorial_data.md) catalog feeds these identifiers.
The presentation 9A03E0, the tutorial transition effects, the media
loading 982270 and the complete wiring of 9827B0/sbdd remain necessary.
No commercial menu, online profile or co-op mode is declared playable.
