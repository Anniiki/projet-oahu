# Global menu-command handler

## Provenance

`VmfFsGlobalContext` reconstructs thirteen of the fifteen branches of
`99B7A0`. The other two stay outside this reconstruction and finish like
unknown codes.
The project's primary reference remains official PC MC 1.66 A, SHA256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The listing used here comes from the Paradise secondary executable, SHA256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.

Private static analysis evidence is in
`reprise-2026-10-31/vmf-host-callback-v2/` and
`vmf-fscommand-global-runtime-v1/native-proof.json`:

- `99B7A0..99B8CC`, 301 bytes, SHA256
  `e4e92c75c91129f1bf18cf201595869ef9a2da5e334749a5515747cd1c6e2aac`;
- `99B8D0..99BABE`, 495 bytes, SHA256
  `7d40b358a818fb8d29b4378ac3bb729c4d0429c63d245a253f9fcc6237db5fee`.

The three alignment bytes between the ranges are excluded. A review reopened
the primary executable in Ghidra 12.2: both ranges and the 246 listing
lines are identical, with checksums recalculated from raw results.
Private evidence: `<private-evidence>/session/`, records 01,
04, 05 and 10. This comparison concerns the file. A
later observation of the official process
separately corroborates the 796 bytes in memory without demonstrating
execution of this handler. The older `445CB0` capture does not substitute
for that. No x86 pointer is called or reconstructed through arithmetic
in the C++ code.

## Reconstructed behaviors

Classification assembles the first four bytes through sign extension,
shifts and OR on 32 bits. Recognized prefixes remain recognized with
a suffix. The provider classifies once at call entry.

| Commands | Handler effects |
| --- | --- |
| `avno`, `cams`, `gein`, `geou`, `wend` | Capture and pass the owner to the named service; a known NULL argument remains distinct from a missing service. |
| `avye`, `clms`, `neav` | Compare state with 2, clear field 38, then write 3 if the comparison was true. |
| `mcin`, `mcou` | Write the mode and/or byte 31 on the captured owner. |
| `tfoc` | Execute the actual query on 32 opaque bytes; zero AL does not read the count. Otherwise capture the count, clear both states and write `count-1` before applying. |
| `tuin`, `tuou` | Preserve save, flag, link and call ordering; reload owners and saved state at native points, including after reentry. |

Called service bodies remain complete dependencies to connect. Their
mere availability is not execution. The `tfoc` local is not interpreted
as a string; the tutorial owner/enclosing relationship is explicit and
does not subtract an x86 address.

## Strings

The `445CB0` wrapper contract defines the view end as a complete C-string
boundary. A command shorter than four bytes cannot produce any
recognized key, regardless of bytes after its NUL. It therefore returns
AL0, without a service or slot, and lets the router continue to the movie.
A partial transport window must be resolved before calling. Low-level
`classify_vmf_command4` still requires four physical bytes and rejects
an incomplete window rather than inventing them.

## Ownership, errors and validation

Objects and inputs are borrowed, stable and serialized; their values
remain live. Unknown data waits at the required read point. A known
NULL retains its native meaning; a mandatory NULL dereference produces
an explicit modern rejection. Already executed effects survive an
error or abandonment, without invented completion or restoration.

The modern pool owns 1 to 32 frames, unrelated to player capacity.
A frame remains reserved during execution and return. Returned pointers
are discarded; no ABA protection is claimed. Do not destroy the pool
or its dependencies during a callback.

Tests cover keys and signed bits, branches, budgets, waits, errors,
reentries, captures and reloads. The composed flow actually uses the
wrapper, owned callback, router and this handler, then a synthetic movie
that writes into the owned dictionary. Short commands and early NULs
reach the movie exactly once. The adversarial bench was rebuilt
and executed on x64 and Win32: after correction, zero failures and zero
divergence, including exhaustive proof for short strings.

These validations prove neither still-missing services, a playable
commercial menu, authentication nor co-op. The measurement without
ordinary `new` concerns only the prepared path tested, not game smoothness.
