# Startup driver 980AF0

This independent x64 C++ driver reconstructs the complete 242-instruction
initialization graph of `980AF0`, through `RET 4` at `980DFF`, from the official
PC MC 1.66 A executable. Reference SHA-256:
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Rea exports provide the graph and instruction evidence; the private
manifests retain their identities. Commercial code bytes are not distributed.

## Reconstructed behavior

The driver preserves reads around callbacks, DWORD allocation arithmetic,
allocator arguments, globals and member writes. It reuses `StartupServiceBase`,
`AllocatorPort`, `find_native_input_slot`, shared input descriptions/records and
`select_native_input_commands`; it does not create another input implementation.

The first loop locates ID 2 descriptions by ordinal and stamps `0.8F` at record
offsets `38` and `3C` for the requested ranks filtered by DWORD type 1. The value
is verified at `F416D0`; its gameplay meaning is not inferred. Rank/pass bounds
are signed. The native record scan uses unsigned base/end pointer comparisons,
not a signed comparison of the declared record count.

The second loop selects rows and cell blocks. It keeps the row counter update,
single-byte empty-name write, table assignment before the null-row refusal,
unsigned pool-limit test and counter updates before accessing storage. Six bytes
per cell are cleared first, then the full block is zeroed and row `+14` cleared
only for a present block. Previous allocations are not released by assignment.

The optional call chain rereads each presence flag at its native test. Member
`+48` becomes 1 only at its end. Final command selection operates on the same
shared records. Successful completion corresponds to native AL=1, not to a
working device, loaded world, new game or co-op session.

## Modern representation policies

- Owners retain allocations, rows, tables and pool backing; links use host-width
  references. Native addresses are provenance identities and are never invoked.
- The 1 KiB frame buffer is initialized to zero. Its native initial contents are
  unspecified. The temporary passed by reference to C4 starts at zero when no
  row loop ran; otherwise the row loop decrements it to zero. Neither policy is
  presented as a measured native write or a successful callee implementation.
- Description counts are modeled separately from owned record extents. Accesses
  are checked as reached, so an early match need not inspect the entire declared
  prefix. Wrapping x86 base/end addresses and aliasing native globals onto record
  bytes are outside this owned representation's equivalence contract.
- Pool storage must cover both the entry loop's `n*8-2` bytes for positive signed
  counts and the wrapped DWORD size used by the subsequent full clear. A cursor
  claiming owned storage without a backing address is refused, even for zero
  bytes; a nonnull empty view remains distinct from the native null cursor.
- Memory clears follow the ordinary forward-direction ABI convention; an
  arbitrary incoming x86 direction flag is not represented.

Malformed extents and null-row dereferences produce explicit modern refusals.
Earlier effects remain, but no AL=1 is fabricated. These refusal paths are not
claims about behavior of valid native input or fixes for the solo loading hang.
Unknown callees, concrete allocation/registry setup and cycle methods remain
explicit interfaces.

## Verification

Build `startup_driver_980af0_test` and run x64 CTest `startup_driver_980af0`.
Synthetic cases cover native branch order, signed/unsigned distinctions, shared
input identity, multiple record tables, pool exhaustion, optional callbacks that
change later presence flags, host-width opaque values and allocation failure.
An independent malformed-backing case fails before its guard is added and passes
afterward, while the nonnull zero-length case stays valid. These checks do not
launch the game or establish controller parity.
