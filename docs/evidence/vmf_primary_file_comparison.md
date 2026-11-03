# VMF: on-disk comparison of the PC windows

## Result and scope

Static comparison of 30 October 2026: **nine windows, 1760 bytes** at the
same VAs in both PEs. All are present, fully file-backed and contained in a
single executable `.text` section. **Only one window is strictly identical:
`0x008DC790/84`**. The other eight differ; none is absent or replaced here by
synthetic zeros.

This equality corroborates, on the official disk file, the 84 bytes of the body
that the secondary analysis associates with WhatZoom. It does not promote its
jump table, its data, its callers or its callees. In particular, the body of
`0x00440BA0/163` called from that route differs. The presence of these bytes
establishes neither their execution nor a reproduced map behavior.

## File identities

| File read in binary mode, without writing | Bytes | Full SHA-256 |
| --- | ---: | --- |
| Official MC 1.66 A, `<installation>/TestDriveUnlimited.exe` | 16797696 | `3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30` |
| Secondary Paradise, `TDU Platinum Update Patch v1.21/Test Drive Unlimited/TestDriveUnlimited.exe` | 12263424 | `f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed` |

Both are PE32/x86, ImageBase `0x00400000`. The official executable has its NT
header at `0xD90`, 9 sections and SizeOfImage `0x01BB0000`; the secondary has
NT at `0x138`, 5 sections and SizeOfImage `0x012AF000`. These are the **file
headers**, with no comparison against a process header. The full hashes and
modification dates are identical before and after the pass.

## Method and anchors

Exclusive private batch:
`reprise-2026-10-30/vmf-primary-file-comparison/`.
`compare_file_windows.py` first checks the two expected SHAs, then the
DOS/PE signatures, the PE32 profile, the section headers and bounds.
For each file, it computes separately
`RVA = VA - ImageBase`, then
`offset_fichier = PointerToRawData + RVA - VirtualAddress`.

The window must fit entirely within SizeOfImage, VirtualSize and
SizeOfRawData of the same section, then within the file. No section
crossing, read of a BSS extension, rebasing, relocation masking, signature
search elsewhere or approximate comparison is allowed.
In both PEs, `.text` starts at RVA/raw `0x1000`, VirtualSize
`0x5EA966`, SizeOfRawData `0x5EB000`; this explains the identical offsets
of the nine comparisons, without assuming that relationship in the computation.

The entries and sizes come from the closed **secondary** Rea exports
`rea/ui-map-input-dispatch/session/`: resolutions 09,10,18,25,30,31,32,33,42.
Each AddressSet holds a single inclusive interval whose `total_bytes`
and `span_bytes` equal the requested size. The comparator checks these
relations and the subject's SHA before using the anchor.

For the identical body, resolution 09:
`ev_58859635df8f9b9738407cc1bea231de00aa9083e66d0fa306a37e5f8d559cee`,
AddressSet `0x008DC790..0x008DC7E3`; assembly 11:
`ev_12302e8be6d5e12ecb7650db453adb12504842a3694c1cfa0c00641cf61e1cfc`.
These are the reused secondary bounds; no new function delimitation by
analysis of the official executable is claimed.

## Exact comparisons

| VA | Bytes | File offset in both PEs | Differing bytes | Conclusion |
| --- | ---: | --- | ---: | --- |
| `0x00442890` | 161 | `0x42890` |159| different |
| `0x00443AF0` | 760 | `0x43AF0` |757| different |
| `0x00443A90` | 83 | `0x43A90` |83| different |
| `0x00443EF0` | 157 | `0x43EF0` |156| different |
| `0x00445560` | 66 | `0x45560` |66| different |
| `0x004455B0` | 142 | `0x455B0` |141| different |
| `0x00445CB0` | 144 | `0x45CB0` |144| different |
| `0x00440BA0` | 163 | `0x40BA0` |162| different |
| `0x008DC790` | 84 | `0x4DC790` |0| full equality |

SHA-256 of each raw window, without normalization:

| VA | Official | Secondary |
| --- | --- | --- |
| `442890` | `abb40a85fe642cb3a865feb536da8afe15843bece389d1f6577b8a66b2868ab7` | `b9da5bd2bbb875113f6fb9166ca47a5293ca8f5fde7fb1f13070149477388d3c` |
| `443AF0` | `6a834ef322c0bb997ad08cb4ea5d717fc53e72fa9f25c56a56cf02af36849739` | `eb9939ea278e59ab1b396baddebef70001984fa4593ab9280b6b438e9f2bec14` |
| `443A90` | `7f9be5841d33ec0d76f2c7d4450a242d0b0e8b7c7170afe426d54de319d66c71` | `3231f79e948336d49255c1e4ac9b07c23ca9702f70ed8c0fa7f9559d5bca8f00` |
| `443EF0` | `67e6e089cfc07f02817d5bc25941dae1ed92d1ac0d4ecec5a9a2a176c08cfb3d` | `3af418e503052e377801cfb51775c9305c152d8ca3d1f0a8500850a09b245ad7` |
| `445560` | `3a9998c02f26c103b49daee2308c6c9a8049fd737c5a1b8351cc2c2a812c9517` | `6759ce745cbb75d6dcfd377427f6c3518212c2cdb4c54227d95358ee29b534bf` |
| `4455B0` | `cfdb45202fa83af6f8a537e869ec929b519b98cc814dc8168c724086add7fc73` | `88612b7c39028211b86d8c7c3119dea8aa34117868be751bac1acc5d3d907e9d` |
| `445CB0` | `50c1b8e00fd5a94ae7e84df69813ccae785a3015ba15750ce035d7ba5b18b2f7` | `7888c9e0ac885ecbdaca9e08bed002c8440e594651576759de728bdc11fd875a` |
| `440BA0` | `bdc512f0aa3d6e94800901105828e2003a9edc1e5614c03804d193308ff1274f` | `d65f37157cc8b5385085b471ad27984d3a7bad3456fb6b58e9e9448d10c89fd8` |
| `8DC790` | `cbe2f69ed958beda6fa5b840e01f3dbf5ad932155bfa0dba96fba75ad4f9c39a` | `cbe2f69ed958beda6fa5b840e01f3dbf5ad932155bfa0dba96fba75ad4f9c39a` |

`comparison.json` also keeps the bounds of each section, the offsets, the
export hashes, the evidence IDs and the first differing offset. No dump of
commercial bytes is written; the document keeps only metadata and
digests.

## Limits for the reconstruction

The eight differences keep these analyses at secondary status for their
compared windows. They prove neither that the routine is absent elsewhere,
nor a semantic equivalence, nor a difference in memory after loading. The
cause of the differences is not examined.

The C++ reader remains validated against the resources and its independent
oracles; this batch adds no primary evidence to the interpretation helpers,
the host commands or the full WhatZoom route. No target game or process, Rea
session, decryption, protection change, build or live probe was launched.
The C++ sources, CMake, Git and the other documents are unchanged.
