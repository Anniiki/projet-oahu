# VMF C++ values and stack

## Contracts and evidence

`tdur_vmf_stack` provides the four observed types: float32 number, linked
text, sprite identity and owned text. Numbers keep their bits until
conversion; sprites use a 64-bit identity and a generation, never the bits
of an x86 pointer.

Sources: Rea exports 21/22/23 from the batch
`rea/vmf-action-interpreter/session/`, `44B200/68`, `44B250/104`,
`44B2E0/103`. Respective evidence:
`ev_cfb37b1741d87cce4ab1e4403900343c10244a4cba53fb4d82aa4d6353dfe86c`,
`ev_4aa3e2c6c6a8b6cf6d728e29dbb475fb17743eacb85d0101f5458486c2080773`,
`ev_89b32227f3c385f689a553d78319654471a40d842712c0fd16c24736890fb064`.
The linked push `44B350/43` completes these contracts. These four bodies are
corroborated by [official trial 25](official_trial_25.md), with no attested
invocation. The simple pop 444600 and the initialization 44B1A0 remain
secondary.

Secondary SHA-256:
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
Official MC 1.66 A:
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The [number](vmf_number.md) and [formatting](vmf_number_format.md) helpers
each keep their own evidence scope.

## References and lifetime

An owned push copies at most 31 bytes, but writes 32 bytes with NUL
padding. Its cursor advances only by `min(length+1,32)`. The pop restores
the previous cursor without erasing the bytes. The returned references
therefore see later writes: after two texts at offsets 0 and 7, replacing
the beginning with `123456789012` yields `89012` for the reference at offset 7.

`VmfTextReference` owns the lifetime of the storage,
without freezing its content.
A fresh resolution sees these changes; a borrowed view does not survive
a mutation. References remain safe after the stack is moved or destroyed.
Operations sharing a storage are serialized.

Number to text conversions share the `VmfTextScratch` explicitly
supplied to their stacks. Two successive numeric pops can therefore modify
the first result. A distinct context owns a distinct scratch.
`VmfLinkedText::copy` provides an immutable owned link,
without the 31-byte truncation.
The lifetime of a mutable host pointer is not invented by this API.
`VmfValue::linked_reference` can now retain an explicitly supplied
`VmfTextReference`, including a mutable arena or a shared scratch.
It takes no raw pointer and refuses a reference without storage.

## Guards and quotas

Creation preallocates the slots and the arena; push/pop/clear do not allocate.
Separate quotas: values, owned bytes, active linked bytes and preallocated
storage. The latter counts `slots*sizeof(VmfValue)+owned_bytes+31`; the 31
extra bytes allow the 32-byte write near the logical limit.
External references kept by the caller remain the caller's responsibility.
Active links are charged according to their stable storage extent from the
offset, NUL/padding included, not according to their current text length.
For a copied immutable source, this extent is length+1; for a mutable
reference, it also bounds future contents. Repeated links count
several times. The pop releases exactly the charged extent, even
if the length has changed. The [dictionary](vmf_variables.md) test exercises
these new integrations; the total of 1993 below describes the initial batch.

A refused push changes neither the values, nor the cursors, nor the existing
references. Overlap of a push source is defined by a prior local
copy. An empty numeric pop yields +0; an empty text pop and a sprite
conversion are explicit errors that preserve the stack. No implicit
per-frame clear is added. These guards replace unbounded native accesses.

## Validation

Private batch `reprise-2026-10-31/vmf-stack-v1/`. Contracts, alias traces and
harness
frozen before the CPP. **1993 checks pass per architecture**, x64 and Win32,
including seven injected allocation failures in the three factories, cross
references, NUL, truncation, quotas, moves and NaN bit preservation.
The earlier formatting oracles also serve as integration tests.
The 512 prepared cycles accumulate neither logical consumption nor ordinary
C++ allocation. They do not prove the smoothness of the final game.

CTest: `vmf_value_stack`. The [first executor](vmf_execution.md) uses this
stack; the game's dictionaries, targets, animations and
commands remain to be connected.
