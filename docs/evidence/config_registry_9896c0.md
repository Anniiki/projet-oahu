# Configuration registry construction

`construct_9896c0` reconstructs the complete driver at `0x9896C0` from the
official PC MC 1.66 A executable (SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`).
The private Rea listing contains 5,439 instructions and 16,626 corroborated
bytes. An independent extraction compared all 405 registration calls, their
receivers, descriptors, tags, name operands and ordering. The integrated revision preserves that
data; its table changes only the indirect-source enumerator and comments.
The public table is a supported subset of 375 of these 405
registrations, kept in native order with unchanged identities.

## Construction and live state

The driver performs its scalar writes and two helper calls, then forwards
every registration of its table: 375 in the public subset. The 320 direct
names are fixed catalogue strings. The 55
indirect names are resolved through an explicit interface at each registration
site, after the preceding callback. Their disk values remain evidence rather
than runtime defaults. An unresolved name refuses the operation; a present empty
name remains valid. Callback identities are labels and are never invoked as
native addresses.

The initial count determines DWORD allocation arithmetic and the first loop.
The second loop rereads the count, source table and destination block around
the callbacks. Replacing the block changes where subsequent writes go. The
opaque caller link at native `+0x8C` uses host width in a separate field and is
written only on normal completion. It is not packed into the 32-bit image.

## Bounds and ownership

Source names are scanned through the byte representation of the entire owned
source array, allowing a NUL in a later entry. Destination writes are bounded by
the actual allocation capacity. A long name can cross an element boundary, as
in the original driver, while remaining within that capacity. Refusals preserve
earlier effects and stop before a partial string copy; they do not truncate.

Allocator interfaces retain ownership of returned storage, including blocks
replaced by callbacks. Resolved name views must remain valid through their use.
This constructor model does not implement the parser destructor or the bodies
of the registration, conversion and allocation helpers.

The ordinary CTest uses synthetic data and catalogue sentinels, with no game
files or private listing. The one-time mechanical comparison remains separate
evidence. Component validation establishes constructor behavior, not INI parsing,
a successfully loaded game world or a working solo profile.
