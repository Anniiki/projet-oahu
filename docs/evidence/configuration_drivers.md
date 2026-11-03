# Configuration loading and section drivers

These independent C++ drivers reconstruct `6B1A00` (82 bytes) and `438450`
(159 bytes) from bounded observations of the official PC MC 1.66 A executable,
SHA-256 `3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.

## Primary corroboration

The official observation `20261030T083003-25076`, PID 9424, creation FILETIME
134378226127793426, retained two 512-byte code windows. Their hashes and all
512 bytes were independently rechecked against the secondary image with SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
Both complete bodies fit inside those windows, corroborating their previously
exported Rea assembly. This equivalence does not cover their other callees.
No additional game launch, import or analysis session was needed.

## Resource loader

The block store owns its bytes and uses host-width logical handles. Presence
and DWORD length remain separate: a present zero-length block is not absent.
Selection outputs are not reset before calling the unresolved selector. The
storage flag is written before branching. The external route forwards the
documented request and returns directly; its outputs and success are not inferred.

Integrated data is decoded in place using the previous decoded byte. A separate
reencoding helper follows the primary inline loop `98EC91..98ECAD`, using the
previous original clear byte. This does not establish the body of `6B19C0`.
Reassigning a handle does not release earlier blocks. Unknown handles and lengths
outside owned storage produce explicit modern refusals without truncation.

## Section driver

The driver keeps callback order, shared stop-byte effects, repeated section
lookups and the final stop-byte reset. The first byte test distinguishes `[` from
leading commands. The return value of the leading command helper feeds the first
section lookup. Later helper returns are also forwarded without treating them as
dereferenceable cursors. The found flag is cleared only before the first lookup.

The parser object, section helpers, embedded selection and external loading
remain interfaces. No actual INI grammar, allocator, filesystem or release
behavior is invented. Modern invalid-input refusals are separate from the native
section driver's AL=1 return.

## Component checks

Run x64 CTest `config_drivers`. Cases use synthetic data and cover separate
presence/size, zero access for zero length, XOR order, external forwarding,
callback mutations, stop conditions and opaque values above 32 bits. A targeted
mutation verifies the leading-command result reaches the first section lookup.
These tests establish neither new-game loading nor functioning game subsystems,
networking or co-op. Commercial data and raw observations remain outside Git.
