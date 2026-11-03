# Startup configuration driver 0x98E960

This C++ reconstruction composes the accepted configuration registry, file
loader, section parser, block store and startup service types. It does not call
native game addresses. The reference is the official PC MC 1.66 A executable,
SHA-256 `3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The bounded official capture of 0x98E960 and the static contract of 0x986B60
were reviewed before integration. Addresses below identify evidence sites.

## Reconstructed behavior

- Assign the owner and clear the service flag before constructing the same
  parser used throughout the run. Reuse the registry registrations of the
  public subset (375 of the 405 native ones).
- Initialize the 256-byte path buffer once. Apply the `game`, `physics`,
  `adddevice` and `replay` overrides at their original sites.
- For GamePC, capture the block and size once. Parse Router, call 0x9A4750
  and 0x9A6040 with the immediate identity `0x10E80C0`, then parse the 15
  sections of the public subset and release the captured block.
- For Physics, capture the block, parse Car, copy 940 of the 944 record bytes,
  clear the original Bike fields and reread the size before parsing Bike.
  Reread the storage flag to choose release or in-place reencoding.
- Call 0x98DE80 even when no Physics block exists. Clear only the first path
  byte for adddevice and select the Car target before testing that byte.
- Load optional Devices and Replay sections, then perform the final buffer
  initialization and parser destruction in native order.

The argument table supplied to 0x986B60 has host pointer width. Its signed
count retains the native 32-bit representation. Both are read again at each
override; native register names do not imply 32-bit host pointers.

## Validation

`config_driver_98e960_test` checks call ordering, shared parser identity,
absent versus present-empty blocks, captured versus reread outputs, neighboring
Physics bytes, release choices, final writes and bounded refusals. An additional
case changes four argument-table addresses above 4 GiB during the run. It fails
four assertions with the original DWORD fields and passes with host-width fields.

An additional composition case uses `ExternalLoaderBinding` and the real
`0x9A5D20` driver. An unreadable service field must propagate through `0x6B1A00`
and stop this driver at GamePC, preserving earlier writes. The native open-failure
path with AL zero must continue through the remaining files and finalization.

Build the target with CMake and run CTest with `-R '^config_driver_98e960$'`.
Use an isolated test environment and the project game guard.

## Remaining boundaries

The auxiliary binding reconstructs argument lookup, the two preparatory drivers
and parser teardown; see [auxiliary startup drivers](config_auxiliary_drivers.md).
Path copying, section grammar, release and other callees remain explicit ports.
The final source at
`0x143F5B6` is an identity, not assumed text. Tests include the real external
binding with scripted low-level ports; they do not load an actual configuration
file or establish a working filesystem implementation.

Modern refusals stop at the invalid access and preserve earlier effects; they
do not invent later native cleanup. The defensive post-Bike size refusal is
unreachable with the current block store because the section parser rejects the
same invalid size first. Completion is a modern result, not a native success
return. This module does not establish a working profile, loaded world or game.
