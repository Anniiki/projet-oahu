# Official trial 24: programs and resource keys

## Identity and scope

Reference PC MC1.66A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Isolated copy, slot 0, PID **30964**, creation FILETIME
**134378597540409344**. Private batch
`<isolated-copies>/slot0/20261030T184840-33116`.

The exclusive `assets-shaders` profile reads eleven code windows totaling
**1,541 bytes** and a table of **2,048 bytes**. The eleven windows are
complete, with no overlap with the patches specific to the trial:

| Verified scope | VA / bytes |
| --- | --- |
| Five Ghidra AddressSets | `601D10/91`, `516F90/433`, `602D00/66`, `504920/5`, `504980/117` |
| Five spans of decoded instructions | `517810/309`, `602DD0/12`, `5169B0/96`, `602AC0/127`, `602940/127` |
| One selected dispatcher span | `6022BD/158`, stopped before the range already observed in trial 23 |

The eleven captures equal the secondary `f452afcd…de6ed` and differ from the
official disk at the same VAs. The `F465D0` table equals both disks and
the 256 independently generated entries; SHA-256
`56bd54524aff39e0c6908390292a97021c2a818bc3ab6fae34ffb72866f54b37`.
Its 256 complete eight-byte scalar reads are not atomic.

## Results and limits

The trial lasts **100.208 s**, then the owned Job ends at the planned timeout
(1460). The slot is released; **6,697 reference files** and **17 source save
files** remain unchanged. No UI input is sent. The observed executable
remains x86 and uses the temporary D3D9On12 bridge; it is not the final native
x64/D3D12 client.

This evidence corroborates the presence of the contracts studied in
[retail_shader_selection.md](retail_shader_selection.md) and the C++
computation of [resource_hash.md](resource_hash.md). It proves neither their
execution, nor the PAINT shaders actually active, nor the uncaptured helpers or
the whole of `602100`. No dynamic program state is invented.

An independent verification rereads the eleven files, the two PEs, the table,
the PID/creation metadata, the hashes and the end of the Job, without any new
access to the process. Private evidence
`rea/shader-selection/runtime24-verification.json`, SHA-256
`84c34b2d991131c2405d7060635f7adf0da3d361692a13ddea92c81d2ca143c2`.
The helper's Rea session is exported and closed; no commercial data is
incorporated into the repository.
