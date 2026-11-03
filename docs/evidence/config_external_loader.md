# External configuration loader

The reconstructed `0x9A5D20` driver uses the official PC MC 1.66 A executable
as its primary reference (SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`).
The existing Rea listing contains 142 instructions and 451 bytes. Its complete
envelope matches the official file, SHA-256
`ea54a3d81b2d4fe3194d7d840f3aff71636812d2341f36782fe187776dd02f89`.
Private evidence retains the listing, byte comparison and branch map.

## Preserved behavior

The block output is cleared before constructing the local reader; the optional
size output is cleared afterwards. The service flag selects the local reader
or a lookup. A failed lookup tests one of two prefixes before falling back to
the reader. A successful lookup with a zero entry size also falls back.

The reader is closed before its size is checked. Allocation uses the original
DWORD addition of `0x801`, alignment `0x10`, and the two forwarded operands.
The block is stored before the null test, and the optional size is written
before the transfer callback. Both lookup outputs survive fallback, including
when the lookup returns zero after writing them.

After transfer, the driver reads the block output again. Failure explicitly
releases that block, clears its slot and preserves the size output. Success
writes one NUL at the captured file size, then cleans up the reader. The raw
eight-bit return value is preserved. Block handles are host-sized indices;
released indices are never reused. Native operand identities are labels, not
callable addresses or host pointers.

## Modern bounds and remaining work

Missing or unreadable inputs, unterminated prefixes, and a NUL outside owned
storage produce a distinct refusal at the access point. Earlier effects remain.
No later native cleanup call or native return is fabricated on that path. This
is a bounded driver model: `LocalFileObject` owns no operating-system resource.
Concrete reader implementations must provide C++ resource ownership independent
of the modeled native cleanup event. Exception unwinding is not reconstructed.
The diagnostic size is zero until a size is read; incoming EDI is not modeled.

The binding forwards the external branch of `0x6B1A00`; that caller ignores the
native AL value. A separate `ExternalCallOutcome` propagates a modern refusal
to `LoaderOutcome::refused_external_call`, so the caller cannot fabricate a
native return after an invalid access. This is not an interpretation of AL:
a valid native return with AL zero still takes the normal caller continuation.
Selection, lookup, file operations, allocation and transfer still
use explicit interfaces. These tests establish driver ordering and ownership,
not successful loading of commercial files, a world, or a new solo profile.

The composition tests cover an unreadable service field through the actual
binding and through the full `0x98E960` driver. They require earlier output writes
to remain visible and prohibit later files, service initialization and native
destruction after the refusal. A local open failure returning native AL zero
must instead continue through all three default file attempts and finalization.
