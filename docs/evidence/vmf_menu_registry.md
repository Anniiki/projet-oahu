# Shared menu-movie registry

## Reference and contract

Menu selection relies on two helpers from
official PC MC 1.66 A, SHA256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`:

- `99A780`, 73 bytes, SHA256
  `a1ac21cf30356a40d197db39597acdb017b5b85eca1d6201c99184683fd49694`;
- `99A9A0`, 58 bytes, SHA256
  `18ac1e0efc0559d671c843e8536fa45ad6d424f838e8a09c024c1541ccbee3a9`.

Static analysis exports establish the rank in EBX for lookup, the
selector in ESI for counting and their common filter: nonzero byte `2E0`,
then 32-bit equality of group `AD4`. Lookup also has historical memory
corroboration; counting is established on disk and
subsequently corroborated in the official process.
The counter returns filtered count M, independently of movie type,
display and `AC4` flags. Lookup starts at the beginning on every call.
An absent rank returns the last match; NULL means no match. Therefore
never terminate a loop solely on NULL.

## C++ integration

The existing `find_vmf_active_menu` helper is retained and moved, without
changing its algorithm, to `vmf_menu_movie.cpp`. The counter shares the
same `VmfMenuMovie` objects. Their `AC4` flags are stored in their own
`state`, without a parallel array.

Metadata and cells remain live: no cursor or cached filtered list.
Native registry creation and destruction remain to be reconstructed.

## Validation

`vmf_get_variable_test` checks the lookup: filtered rank, last-match
fallback for an absent rank and NULL when no entry matches. The counter
has no dedicated test in this tree.

These checks prove neither execution of these helpers in the commercial
game nor playable commercial menus.
