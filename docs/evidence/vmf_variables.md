# VMF variable dictionary

## C++ component and scope

`tdur_vmf_variables` reconstructs the coalesced table used by the menu
scripts. `VmfVariables` provides lookup, bitwise numeric write, inline
text, text link, push onto `VmfStack`, text copy and scope cleanup. Optional
indices replace the x86 pointers: index 0 is a real successor, distinct from
the absence of a link. Scope 0 is valid.

This service stays independent of the renderer and of the native pointer
layout. It is wired to the [script reads](vmf_get_variable.md) and to the
optional [display write/cleanup](vmf_variable_lifecycle.md) backend.
The commercial menus remain to be validated after the text and focus services.
Type 5 is recognized by copies and consumers; no type 5 producer is invented
or exposed by a setter.

## Sources

The Rea exports studied come from the **secondary** executable, SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`:

| Contract | Original VA | Private cache (`<private-evidence>/`) |
| --- | --- | --- |
| Creation, first free slots | `443320`, `443410` | `reprise-2026-10-31/rea/vmf-text-services/session/16`; `reprise-2026-10-31/rea/vmf-host-context/session/14` |
| Typed copy | `443430` | `reprise-2026-10-31/rea/vmf-host-context/session/15` |
| Insertion and lookup | `4434B0`, `4435D0` | `reprise-2026-10-30/rea/vmf-action-interpreter/session/109`, `108` |
| Push and text conversion | `443620`, `4436A0` | same batch, `/58`; `reprise-2026-10-31/rea/vmf-runtime-scheduler/session/07` |
| Number, text and link setters | `443700`, `443770`, `4437F0` | `reprise-2026-10-31/rea/vmf-host-context/session-v2/29..31` |
| Cleanup | `4438D0` | `reprise-2026-10-31/rea/vmf-display-lifecycle/session/57` |

The numbers designate the `*-procedure_assembly.json` exports, whose
EvidenceIDs are kept in the private receipts. The [hash](vmf_variable_hash.md)
has its own official corroboration of 114 bytes. A [separate trial of the
dictionary](vmf_variables_runtime.md) now corroborates the eleven
procedures above: twelve exact ranges, 1479 bytes. It proves their
presence in the instrumented official copy, not their invocation.
The official PC MC 1.66 A executable remains the primary reference, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.

## Preserved behaviors

Names are hashed and compared whole, then stored in 31 bytes. A second
write of a long name can therefore create a duplicate; the rehash of its
truncated key can move an occupant and miss its predecessor. The setters
use the native insertion directly, without a prior lookup that would mask
these effects. The comparison follows the host CRT (`LC_CTYPE`); non-ASCII
equivalence with MSVCR71 is not established.

Collisions take the first physical hole from 0. A move copies the typed
content and repairs the predecessor when it exists. An overwrite preserves
key, scope, successor and entry counter.

Cleanup scans the physical indices and shifts the chain content, preserving
the links up to the new tail. It rereads the same index after compaction.
A requested scope of 0 selects the **non-zero** scopes; entries of scope 0
are preserved. The native artifact that lets a moved entry survive behind
the scan is documented and tested before any future intentional correction.

`erase_scopes` preserves this order call by call within a common transaction,
without additional allocation. The `table_full` code distinguishes the NULL
return of the native insertion from the modern quotas; the script wrapper
must still arm flag 200 in that case.

## Linked texts and modern guards

A link explicitly retains an owned `VmfTextReference`, without copying its
content. It may observe a reuse of the arena or of the stack's numeric
scratch. This assumes no particular native provenance for the pointer
supplied to 4437F0. References remain safe after their source is destroyed.

Link quotas count the stable extent of the storage from the offset, NUL and
padding included. Growth of the shared text therefore does not overflow
the counter at pop. Repeated references count several times; a snapshot
kept by the caller falls under the caller's budget.

Creation preallocates two tables and the name scratch. Prepared operations
allocate no ordinary C++ memory. The second table allows a transactional
cleanup: a chain to compact that is cyclic, invalid or passes through an
empty slot => error without mutation of the active table. This guard
replaces an
unsafe native access, notably after removal of a long, badly linked key.
Storage limits and a too-small text destination are also refused
explicitly. All accesses to the same dictionary and to the shared mutable
storages must be serialized.

## Targeted validation

`vmf_variables_test` replays **37 states from the eight independent scenarios**,
frozen before this implementation. The source oracle has SHA-256
`7143b0f32f69b894c487fcaa4c78ce6de4d14c755eafcd46e28b63d3eb062026`.
The fixture loads no commercial data and does not import the production
C++. Additional cases cover typed copies, mutable links, lifetimes, quotas,
atomic refusals, `PAGE_NOACCESS` pages, NaN bits, injected allocations and
prepared cycles without logical accumulation.

Build `vmf_variables_test`, then run CTest
`-C Release -R '^vmf_variable_dictionary$' --output-on-failure`. The private
reports are in `reprise-2026-10-31/vmf-variables-v1/`: **1779 checks pass
in each architecture**, x64 and Win32, including five injected allocation
failures. The six targeted suites pass in both builds: dictionary, stack,
scalar execution, timeline, targets and frameplayer. These service checks
do not constitute a trial of playable menus or of multiplayer.
