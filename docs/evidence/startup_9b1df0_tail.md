# Final allocation block of startup service 9B1DF0

Reference: official PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The candidate PE table is `FA4320`, controller index 9. No runtime class
identity is inferred from that candidate.

## Established scope

This component implements only `9B1FA8..9B2063`. It reloads the allocator object
before each of three slot +20 calls with `(0x18,0,0,0)`. A null result skips that
allocation's opaque callee and still continues to the next allocation. The three
results are stored at native fields +9C, +A8 and +AC. Fields +A0 and +A4 are
cleared between the first and second allocation. The final count and two arrays
are cleared before the successful return.

The three opaque callees have no usable body established here. Ports receive the
values placed in EDI/EAX and supply the post-call EDI value consumed by the caller.
This records an unresolved dataflow contract; it does not implement or execute
those callees. Values remain x64-capable and are never dereferenced as native
addresses. A missing allocator object causes an explicit modern refusal while
preserving prior writes, instead of reproducing a null dereference.

The [complete caller driver](startup_9b1df0.md) now reuses this block. Its separate
early-return branch returns false after a nonzero `610840` result and a `610900`
call. This component alone must not replace that whole initializer.

## Evidence and limits

Rea Listing graphs contain 242 instructions for `980AF0` and 186 for `9B1DF0`.
An independent check matched every instruction and evidence identifier to the
original observations, checked entry reachability and verified that all
intraprocedural edges stay within the observed graphs. Opaque calls remain
outside that closure claim. The terminal block's graph rows cite their individual
primary evidence identifiers; raw evidence remains local.

The candidate startup gate `504820` has only a nonordinary call to an out-of-image
target established. That target was not followed and no gate implementation is
provided. No meaning is inferred from the other excluded nonordinary callees.

## Component validation

Run the x64 CTest `startup_9b1df0_tail`. Cases cover all eight combinations of
null/non-null allocation results, changed allocator identities, opaque values
above 32 bits, callback-visible store order, preserved callback writes, bounded
array clearing and refusal after partial effects. No retail code or game process
is invoked by these tests. This component does not prove new-game loading,
gameplay, multiplayer or co-op operation.
