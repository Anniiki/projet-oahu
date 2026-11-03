# Auxiliary startup configuration drivers

This module reconstructs the related helpers `0x986B60`, `0x988FC0`,
`0x98DD20` and `0x98DE80` from the existing official PC MC 1.66 A Rea listings.
Reference SHA-256:
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Native addresses identify evidence or operands; they are never called.

## Reused components and ordering

The auxiliary drivers share the accepted configuration registry and the
375 registrations of its public subset, block store, loader and section
parser. Each frame owns one parser
from construction through teardown. The binding routes `0x98E960` through the
rebuilt helpers and forwards only the remaining unknown operations.

- Argument lookup captures a host-width table and a signed 32-bit count. It
  copies the first matching option's following value. A trailing match is
  ignored; a value may itself start with a dash. Comparison and copying remain
  explicit interfaces without assuming C runtime semantics.
- Teardown resolves owned four-byte prefixes instead of dereferencing a raw
  pointer minus four. It preserves the captured element header, the unchanged
  `+0x88` link, the later read of `+0x80`, and native callback/write ordering.
- `0x98DD20` uses a 256-byte path buffer and eight sections, then tears down
  its parser even when no file exists.
- `0x98DE80` first checks its signed guard, then handles the FA9Dxx table,
  three fixed sections and one formatted section. The `0x985610` record loop
  also runs without a file. Its count and base are absolute globals, reread
  at their original sites.

## Control flow and boundaries

Modern refusals propagate through typed results, including through the helper
binding into `0x98E960`. Earlier effects remain; later native effects are not
fabricated. A native return with AL zero and the positive-guard return remain
ordinary native returns.

The `+0x80` link has host width and an explicit unestablished state. Its writer
is not reconstructed; a concrete constructor port must establish it. Unknown
labels, comparison, file operations, formatting and callbacks remain ports.
The modeled table name is bounded to its owned 32-byte field; a longer native
copy is refused instead of spilling into a neighboring entry. This is a modern
bounds policy, not a claim that the original routine checked that length.

Tests use owned synthetic data and scripted low-level operations. They validate
the composed drivers, not commercial file loading, a working profile, a loaded
world or restored multiplayer.
