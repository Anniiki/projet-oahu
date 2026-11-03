# Notification data lookups

The three lookups `9A9480`, `9A92B0` and `9A9340` are reconstructed in C++
with explicit DWORD coordinates and typed data views. They prepare the wiring
of the `89BF80` effect; they do not yet load the game's commercial tables.

## Reference and evidence

Primary reference: official PC MC 1.66 A executable, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The Rea evidence already exported and closed agrees with the complete bodies
of the primary and the secondary: 49, 48 and 256 bytes. The first routine has
two distinct ranges; its three padding bytes are excluded from the 49 bytes.
Private evidence is in
`<private-evidence>/reprise-2026-10-31/vmf-notification-database-runtime-v1/`.
This batch reuses that evidence, with no new call into the game and no new
analysis of the executable.

## Preserved behaviors

- `find_vmf_notification_record_9a9480` walks rows of stride `0x1F8` and
  returns the first key at `+0x1F0` equal to the supplied 32-bit key.
- `find_vmf_notification_name_9a92b0` uses a stride of `0x94`. Its empty-table
  guard precedes the read of the key pointer; the inline text at `+0x0C` is
  never inspected by this lookup.
- `find_vmf_notification_group_9a9340` checks its argument before the global.
  A NULL argument returns NULL; an unknown argument remains an explicit
  error. After the table guard, its key is compared against the fifteen
  references of each row, in the order `0x18, 0x20, …, 0x88`. Reaching a NULL
  reference is an error, with no skip to a later match. The text at `+0x10`
  remains data for the future caller.

The `base + count * stride` ends and the cursors use modulo 32 arithmetic.
An unsigned `base >= end` guard ignores useless data, even with a non-zero
count. A lookup stops at the first result and does not prevalidate the
following rows. The modern budget is 100000 rows by default; exhausting it is
an error, never an absent result.

## Validation and limits

The suite contains **409 active checks in Release/NDEBUG**: every position of
the fifteen references, prefix errors, unknown data, NULL values, modular
counts, exact budgets, truncated rows and chaining of the lookups. An earlier
review reproduced and then corrected an unknown argument mistaken for an
absent result on an empty table. The proposed tests also required the
correction of a fixture identifier and a direct include.

```powershell
cmake --build build --config Release --target vmf_notification_database_test
ctest --test-dir build -C Release -R '^vmf_notification_database$' --output-on-failure
```

Spans, references and text views borrow stable storage supplied by the
caller. Serialization of accesses and lifetimes are contractual. These
structures represent neither a native ABI layout nor a file format. The
fixtures are synthetic; they prove neither playable menus nor co-op.
