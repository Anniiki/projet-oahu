# Shared menu-movie registry

## Reference and contract

The [global handler](vmf_fscommand_global.md) relies on two helpers from
official PC MC 1.66 A, SHA256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`:

- `99A780`, 73 bytes, SHA256
  `a1ac21cf30356a40d197db39597acdb017b5b85eca1d6201c99184683fd49694`;
- `99A9A0`, 58 bytes, SHA256
  `18ac1e0efc0559d671c843e8536fa45ad6d424f838e8a09c024c1541ccbee3a9`.

Private exports establish the rank in EBX for lookup, the
selector in ESI for counting and their common filter: nonzero byte `2E0`,
then 32-bit equality of group `AD4`. Lookup also has historical memory
corroboration; counting is established on disk and
[subsequently corroborated in the official process](official_menu_actions_runtime.md).
The counter returns filtered count M, independently of movie type,
display and `AC4` flags. Lookup starts at the beginning on every call.
An absent rank returns the last match; NULL means no match. Therefore
never terminate a loop solely on NULL.

## C++ integration

The existing `find_vmf_active_menu` helper is retained and moved, without
changing its algorithm, to `vmf_menu_movie.cpp`. The counter shares the
same `VmfMenuMovie` objects. Their `AC4` flags are stored in their own
`state`; the FSCommand cell type is an alias, without a parallel array.

`VmfFsMenuCollection` supplies both services to the global handler.
It borrows a fixed physical range of caller-owned movies. The counter's
unsigned32 result is reinterpreted as signed32, as in `99B7A0`. Signed-rank
bits are passed unchanged to the existing helper. A range exceeding the
native unsigned32 physical count is rejected before reading; no enormous
fake array is created to test this boundary.

Metadata and cells remain live: no cursor or cached filtered list. The
global context retains its initial count and already selected cell
during a wait; the next lookup rereads the filter. This modern serialized
resumption does not establish concurrency in the original game. The
range, its movies, their displays and the service must remain alive
and stationary until all borrows finish. Native registry creation/destruction
and external services `866040`/`866080` remain to be reconstructed.

## Validation

`vmf_fscommand_collection_test` exercises the discriminating
cases on the actual provider: double filter, 32-bit groups, repeated
rank, fallback, empty registry, unknown cells, changes between resumptions
and cancellation. The `frok`/`frca` loops also check call and write counts:
additional passes over an already cleared cell would be detected. Two
synthetic PC 2.8 and Xbox 2.5 VMF scenes connect selection, flags and the
same display's dictionary. The final service is a declared test double.
Under MSVC `/std:c++latest /W4 /WX`, all 332 checks pass in Release x64
and Win32. After rebuilding the 55 VMF test executables, all 67 VMF
suites pass on each architecture without failure. Other project tests
are outside this validation.

```powershell
cmake --build build --config Release --target vmf_fscommand_collection_test
ctest --test-dir build -C Release -R '^vmf_fscommand_collection$' --output-on-failure
```

These checks prove neither execution of the counter in the commercial
game, external services, playable commercial menus nor co-op.
