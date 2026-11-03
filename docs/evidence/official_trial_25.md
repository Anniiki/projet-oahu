# Official trial 25: VMF script core

## Result and identity

The isolated copy of **TDU PC MC1.66A** provides **28 code windows, 5,496
bytes**, corresponding to 24 studied procedures, and **six four-byte
scalars**. Every window equals the analyzed secondary; none equals the
official file at the same VAs. The constants equal both disks and the Rea
listings. This corroborates the presence of the code, not its execution.

Official: SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Secondary:
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
Workspace slot 0 (coop-anything), private batch
`<isolated-copies>/slot0/20261030T220251-29060`, PID **34200**,
creation FILETIME **134378714058315318**. The image is at `0x00400000`.

## Exact perimeter

The exclusive `ui-vmf` profile of `tdur_probe_code` uses only these ranges:

| Role | VA / requested bytes |
| --- | --- |
| String to number conversion | `442530/125`, `4425B0/109`, `442620/157`, `4426C0/44` |
| Stack: pop number, pop text, push text, push link | `44B200/68`, `44B250/104`, `44B2E0/103`, `44B350/43` |
| Add, Subtract, Multiply, Divide | `444630/275`, `444750/275`, `444870/275`, `444990/294` |
| Equal, Less, And, Or, Not, ToInteger | `444AC0/278`, `444BE0/270`, `444CF0/341`, `444E50/341`, `444FB0/204`, `445430/115` |
| GetVariable, SetVariable | `445A60/305`, `445BA0/257` |
| Checker, dispatcher, advance, rebase | `442890/161`, `443AF0/512` + `443CF0/248`, `443A90/83`, `443EF0/157` |
| Jump, If, static FSCommand | `445560/66`, `4455B0/142`, `445CB0/144` |

The conversion occupies four Ghidra intervals totaling **435 bytes** within
a span of 444; the nine intermediate alignment bytes are excluded. The
contiguous 760-byte dispatcher is split into two reads to respect the
512-byte ceiling per call. No other protocol/physics catalog is read.

The six scalar reads, in data pages of the image, are:

| VA | Raw LE value / established usage |
| --- | --- |
| `F4165C` | `0000803f`, float32 +1 |
| `F41664` | `000080bf`, float32 −1 |
| `F4166C` | `00002041`, float32 10 |
| `F41B08` | `95bfd633`, division threshold |
| `F05CE4` / `F05FD0` | `31000000` / `30000000`, boolean strings "1" / "0" |

The reads do not form an atomic snapshot. There is no free address in the
CLI, no pointer walking and no read of activation data.

## Lifetime and preservation

The trial lasts **100.134 s**; the owned Job reaches its timeout (1460), then
the slot is released. **6,697 reference files** and **17 source saves**
remain unchanged. No menu, keyboard or mouse control is sent.
The observed game remains x86 and uses the temporary D3D9On12 bridge; it is
not the future x64 client with a direct D3D12 renderer.

## Verification and limits

`reprise-2026-10-30/vmf-core-runtime-25/verify_runtime.py` rereads the
captures, the two PEs, the Rea AddressSets, the constants and the end
reports, without new access to the process. Its result
`runtime25-comparison.json` carries SHA-256
`74e114d2403b30e6c85d4d1ada2745577ac837cd64b34dcc376de2f993647cdc`.
It checks the virtual/file bounds and the section type, the 28 exact
requests, the fingerprints, PID/creation and the agreement of the saves.

The new bounds come from the Rea61..77 resolutions of
`rea/vmf-action-interpreter/session/`, the seven older procedures from the
`rea/ui-map-input-dispatch/session/` batch. The scalars are exports
40/41/42/78 and the first four bytes of 18/19 of the new batch.

An independent review recomputes these relations without importing or
rerunning the verifier and recovers the 40 artifact fingerprints. Private
evidence `vmf-core-runtime-25-independent-review/review-v1.json`, SHA-256
`5e843cf203f53fc29f78762c1e738e8127870027db007c034c1e664e16f4dcb4`.
It confirms the consistency of the produced audits, without claiming to have
rehashed the 6,697 references itself afterwards. The reader checks the
module's protections and base allocation, without a separate `MEM_IMAGE` test.

This corroboration does not extend to the absent callees: variable
dictionary, target resolution, formatter `442330`, CRC `4426F0`, game
callback or locking. GetURL2, Call and the buttons are not captured here.
This trial does not demonstrate the execution of the scripts by the C++ client.
