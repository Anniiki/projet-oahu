# Textures and samplers: the MAT/PAR/LAY boundary

## Result and level of evidence

The static path is closed for **type 2 parameters**: MAT selects PAR, PAR
designates LAY, each LAY slot matches a raw eight-byte key against the internal
BMAP name of a `.2DB`, then supplies a texture pointer and four sampler bytes.
The slot number becomes the D3D9 sampler number.
This conclusion combines official assets with routines analyzed in the
secondary. **Official trial 23 now corroborates twelve ranges of this code in
memory**, within the bounds given below. It does not prove their execution, an
active shader or a material rendered faithfully by the new client.

Both x86 PEs have a preferred base of `00400000`:

| Source | Bytes | SHA-256 |
| --- | ---: | --- |
| Official PC MC 1.66 A, `<installation>/TestDriveUnlimited.exe` | 16 797 696 | `3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30` |
| Local secondary, Platinum patch 1.21 | 12 263 424 | `f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed` |

Trial 21 corroborated the seven material ranges described in
[retail_material_bindings.md](retail_material_bindings.md), including the
**span** `504B10/92`. Trial 23 supplies the new bounded corroboration. The
fourteen ranges compared on disk in this batch all differ from the official
PE; twelve of them are captured in trial 23. The equality of a caller with an
official capture does not propagate to its callees. The verification work here
launches no game and opens no process; it rereads the closed files of the
trial conducted separately.

## Address conventions and parameter selection

Asset offsets designate the **start of the chunk**, including its 16-byte
header. Relocated x86 pointers and file offsets are distinct; the examples
below keep the offsets before relocation.

`602100` receives a MAT payload. Its excerpt `60235B..60239F` tests the WORD
`MAT+12` and calls `509590(0)` with `ECX = MAT+10`. The accessor `509590`
only checks that the count is non-zero, then returns
`*(MAT+E0+4*k)+10`. For `k=0`, this is the payload of the first PAR.
It does not check `k < count`; this guard is missing from the general native
contract.

`602100` then reads the DWORD `PAR+10`:

- `1`: a distinct path, `601EB0`, then `601D80` with a per-stage LAY
  selection; its grammar must not be equated with type 2;
- `2`: call to `601F90` with the PAR payload;
- any other value: explicit failure of this branch.

For type 2, `601F90` reads `PAR+54` as the LAY pointer. Constant processing
precedes textures; the latter are skipped when the global mask
`[FE77C0]+24 & 42h` is non-zero. What these bits mean for the game is not
established. In the three official members studied, the **108 MAT** each have
one parameter and the **108 PAR are type 2**. This does not generalize to the
corpus. Only the dispatch excerpt `60235B/69`, not all of `602100`, is
corroborated in trial 23; the details of the type 1 path remain secondary.

## LAY slot contract

`505060` and `601F90` read the count as an **unsigned BYTE at LAY+1C**,
not as a WORD or DWORD. Slots are **32 bytes**, starting at `LAY+20`.
The eight locations of `120h`-sized LAYs are not all active.

| Offset within a slot | Established read or write |
| --- | --- |
| `+00..+07` | Eight preserved bytes; they are used neither by the BMAP comparison nor by the binding loop studied. |
| `+08..+0F` | Resource key compared as two DWORDs, without conversion to a path or string. |
| `+10..+13` | Pointer cache to the **BMAP chunk**; written by `505060`. |
| `+14..+15` | Not interpreted here. |
| `+16` | Common MIN/MAG filter index in `601F90` and `601D80`. |
| `+17` | MIP filter index. |
| `+18`, `+19` | U and V addressing indices. |
| `+1A..+1B` | Not interpreted here; `+1A=1` in the samples does not prove a filter. |
| `+1C..+1F` | Auxiliary pointer tested/dereferenced by `5050E0` (secondary, not captured), therefore not four additional sampler bytes. |

The routines studied ensure neither `count <= chunk capacity` nor the bounds
of all table indices. An independent reader must check the physical bounds
before exposing these fields and refuse unknown encodings, without reproducing
an out-of-bounds read.

## Exact resolution to BMAP

`505060` receives in `ECX` a material collection whose `+18` designates LAYA,
and on the stack a texture collection whose `+0C` designates BMAP. It walks
the contiguous LAY chunks and each of their active slots. The instructions
`505093..5050B7` compare:

`slot[+08,+0C] == BMAP[+10,+14]`.

This internal BMAP name corresponds to the **eight bytes `.2DB+20..+27`**.
On equality, `slot+10 = BMAP`. All matching slots are updated; a miss leaves
the previous cache unchanged. There is no file name comparison, no mapping of
`DETAIL`/`NORMAL` to a `.2DB` suffix, no case normalization and no "take the
first texture of the bank" rule.

`505700` applies this rebinding to the loaded elements of a texture
collection; `505750`, which remains secondary, propagates it to material
collections. The cursor of `505700` only advances when the current element is
loaded. Other direct callers of `505060` exist (`6F0E10`, `8504F0`) but their
full availability policy and priority between banks are not established here.

The nominal callback of the MAT dispatcher is not enough to explain the
resolution: `504B10` calls `[FAD8E0]->vtable+38`. The nominal wiring described
earlier leads to `614DF0`, whose exact body is **`RET 4`**. Likewise, the
entry `512400`, which remains secondary, is a `RET`, even though the
decompiler attaches another body sharing this exit. The live identity of
`FAD8E0` and the effective order of registrations remain to be corroborated;
the name of a decompiled function does not replace this evidence.

## GPU texture and fallback behavior

In `601F90`, slot `i` first handles its texture, then its samplers:

1. Non-null BMAP cache: pass `BMAP+10` to `5FC380(i, ...)`.
2. Null cache and non-zero eight-byte key: `512490` returns the fixed
   address `FAD8F0`, passed to the same helper. The visual nature of this
   fallback is not known; it is not called a "white texture" or "checkerboard".
3. Null cache and null key: **no SetTexture for this slot**, so the previous
   state may persist. This path does not systematically disable the stage.

Let `P = BMAP+10 = .2DB+20`. `5FC380` uses `P+14`, that is `.2DB+34`, as the
runtime GPU pointer. If it is null, it calls `512980(P)`; if `BYTE[P+20]` is
zero, it calls `5F2890(P)`. It then compares this pointer with the cache
`FAF410+4*i`, then calls the D3D9 vtable slot `104h` (**SetTexture**) when
necessary. The pointer kept in the LAY slot is therefore that of the source
chunk, not directly an `IDirect3DBaseTexture9*`.

`512980` goes through `512620` for creation, then conditionally through
`5F2890`. `512620` reads width/height at `P+08/+0A`, bounds the mip count
`P+0E` to the number of shifts of the larger dimension, and selects creators
through bits 11 and 6 of `DWORD[P+10]`. The full format translation, GPU
capabilities, transfer success and the special cases of these creators are not
closed by this investigation. The captured call instructions do not
corroborate the bodies of the creators called, of `5F2890` or of its uploader
`5126A0`, which remain secondary.

`5FC380` does not report creation failure to the caller; the SetTexture
HRESULT is not tested before the software cache is updated. A resolved key
therefore proves neither a successful upload nor a texture actually sampled.

## Sampler translation

`601F90` passes the ordinal number `i` and the slot indices to `5F5A30`.
This helper compares its cache of **56 bytes per stage**, then issues the
required SetSamplerState calls in this order: **U, V, W, MIN, MAG, MIP**.
W is fixed at **3 = CLAMP**; MIN and MAG receive the same argument.
HRESULTs are not tested before the cache is modified.

The identification of the COM slots comes from the local Windows SDK headers
`10.0.26100.0/shared/d3d9.h`: SetTexture is method 65 (`104h`) and
SetSamplerState is method 69 (`114h`). The constants of `d3d9types.h`
confirm ADDRESSU/V/W = 1/2/3, MAG/MIN/MIP = 5/6/7, LINEAR = 2,
WRAP = 1, MIRROR = 2, CLAMP = 3, BORDER = 4.

For addressing indices `0..4`, the table is **`[1,1,3,2,4]`**: two encodings
of WRAP, then CLAMP, MIRROR, BORDER. `601C20` also exposes this conversion
for the other parameter path. No native bound is implicitly added to this
table.

The first filter entries on the stack of `601F90` are `[2,1,2]` for MIN/MAG
and `[2,0,2,3]` for MIP. This is a numerical transcription, not a guarantee
of GPU validity for every index or combination. The **543 active slots
observed all have `+16=0` and `+17=0`**, so this route requests LINEAR for
MIN/MAG/MIP. Their U/V encode WRAP, CLAMP or MIRROR. Neither maximum
anisotropy, nor sRGB, nor association with a particular shader can be deduced
from these four bytes.

## Official examples and counterexamples

Hexadecimal offsets relative to the `.2DM` member, not to the whole bank:

| Bank | MAT `PAINT_LR` | PAR | LAY | Slot | Raw BMAP key | Matching physical `.2DB` member |
| --- | --- | --- | --- | ---: | --- | ---: |
| Alfa_GT | `63D0` | `35D0` | `0FE0` | 0 | `424F44595F4C5200` | 4, 256×256 |
| 300_C | `73A0` | `44D0` | `18E0` | 0 | `424F44595F4C5200` | 15, 256×256 |
| A3_V6 | `7050` | `4010` | `1580` | 0 | `424F44595F4C5200` | 7, 256×256 |

The first block of these slots contains `DETAIL`, the second `BODY_LR\0`.
It is **the equality of the second block** with BMAP that establishes the
link. The same internal key in three banks does not set a global priority; the
rebinding uses the collection actually passed to `505060`.

| Bank | LAY | Active slots | Non-zero keys | One local BMAP match | No local match |
| --- | ---: | ---: | ---: | ---: | ---: |
| Alfa_GT | 34 | 169 | 73 | 27 | 46 |
| 300_C | 37 | 187 | 81 | 37 | 44 |
| A3_V6 | 37 | 187 | 79 | 35 | 44 |

The LAY counts are 1 or 6; the rest of the observed DWORD is `00 CD CD`,
which is also a counterexample to a 32-bit reading of the count.
The BMAP caches and auxiliary pointers are null in these slots on disk.
`CUBEMAP\0` is an example of a key without a match in the same banks.
**134 local matches are missing out of 233 non-zero keys**: this proves
neither corruption, nor a specific external texture, nor dynamic generation.
A global resolution, a procedural substitution or a fallback must be
established through their own path before being implemented.

## Preserved evidence and remaining boundary

Private evidence: `<private-evidence>/reprise-2026-10-30/rea/texture-bindings/`.
The single session reuses the closed `material-bindings` snapshot, then is
**exported and closed** after 72 operations. The bundle keeps 1 013 pieces of
evidence, including the imported ones. `verify-findings.py` checks the two PE
SHAs, the AddressSets, the retained raw reads, the before/after banks and the
asset results; it opens no process. Results are in
`disk-comparisons.json`, `asset-native-contract.json` and `evidence-index.json`.

| Rea evidence | Subject |
| --- | --- |
| `ev_61d114575182bf018177f25324f5752b171c16b6fceffa2b1c090204f1ace76a` | `15`, assembly of `505060`, keys and BMAP cache |
| `ev_cfb8f8b65767e5ffe2deb597d011fc7f1aa03214fe3e1f16777837f015fa2867` | `63`, assembly of `509590`, MAT → PAR |
| `ev_0d81117de08a1a242a428520f29a9b4013c68d8b311dd93f07d6b600aa2333a7` | `59`, assembly of `602100`, PAR type dispatch |
| `ev_6184dbb9e820339ed96fd881e3da89c4cd9bc1d447ca18f0cb38d0900d6c31c8` | `44`, assembly of `601F90`, PAR → LAY → ordinal binding |
| `ev_baa0680b56a1772b6ee980d9d670482f094953bb3df522d4090f432d9ef41e09` | `26`, assembly of `5F5A30`, samplers and cache |
| `ev_e9ef5a6c6cef5bff34250a4c9d17004ca666f78e670fbc37a4244d66dc657208` | `52`, assembly of `5FC380`, runtime texture and SetTexture |
| `ev_bbf5b866db2aac2c97d09640983ba186ea1a1a1492d79d04d2bb7cfc083c361c` | `11`, `614DF0 = RET 4`, limit of the nominal callback |
| `ev_0314c5f5339a2117da27290b7b9d65bf89890756e9c23581cb33c62625b6bf9f` | `60`, assembly of `5050E0`, auxiliary slot pointer |

`runtime-catalog-v1.json` supplied the **12 reads / 1 258 bytes** of the
exclusive `assets-textures` profile integrated separately.
It does not contain the 84 earlier ranges. The private catalog keeps the
secondary hashes, sections, EvidenceIDs and bounds; this work modified no
probe.

## Trial 23: bounded official corroboration

Closed trial: `<isolated-copies>/slot0/20261030T174712-27428/`.
PID **33928**, creation FILETIME **`134378560639742868`**, identical in
the suspended PID, the launcher report and the code observations. Base
`00400000`, SHA of the official file rechecked, file locked during the
trial. Reported duration **120.186 s**, closed by the Job object at the
timeout, code **1460**. The postflight confirms that the slot was
released, that the **6 697 reference files** are unchanged and that the
fingerprints of the **17 saves** are identical.

The independent recheck rereads the twelve `.bin` files, the two complete
PEs, the private catalog, the Rea results and the closed reports. It
recomputes the SHAs, the byte-by-byte differences against the official disk
image and the bounds. The twelve ranges are readable, **exactly equal to the
secondary** and all different from the official file on disk:

| VA / bytes | Corroborated coverage | Observed SHA-256, equal to the secondary |
| --- | --- | --- |
| `00505060 / 121` | Complete AddressSet: rebinding by BMAP key | `7b10ba8ecab9112644c90668809497e06ada586899fee7472cb7c341e03114aa` |
| `00505700 / 71` | Complete AddressSet: collection walk | `ac2a2aa67aa88b0f498f12a560bca4c895d5c6c7621f26c29f12aa74bb0ac1f3` |
| `00509590 / 29` | Complete AddressSet: PAR payload access | `aab5e341af1342eb603339d45e5d263a4572915d51b7e2908cbb6cc1162ab32e` |
| `00601F90 / 363` | Complete AddressSet: type 2 parameter binding | `0310cce6bf94ccf6154403c39784fedbeaf6ae44c9efe166e35721bb09c591e3` |
| `005F5A30 / 246` | Complete AddressSet: SetSamplerState and caches | `3a3f2368437eac4945a9764d750ee463fc735803428b09d2156afc7ab5c59e99` |
| `005FC380 / 126` | Complete AddressSet: deferred creation and SetTexture | `978e0ab21a8c30441316c2c3aa498fd12d26d75e497e0e4cd4a31bad2830bf0b` |
| `00601C20 / 51` | Complete AddressSet: addressing table | `84aa886540b2365178657aab9b62c23a2defe4dd350bf1144b22468231cbf924` |
| `00512620 / 128` | Complete AddressSet: creator selection | `952ac53e8e358dd629ca51e98a478d81a826ed39c03714c3d3e0edc6dd892eb0` |
| `00512980 / 45` | Complete AddressSet: creation then conditional init | `4fa5e258e181851059886253af207031503baa7f2dbb710a40e27952f7893db2` |
| `00614DF0 / 3` | Complete AddressSet: nominal `RET 4` | `e598d0c3ba86d917b177d7adde0556aa99bc355543c57aee0a3e50b684dd7e99` |
| `0060235B / 69` | **Selected span** of the dispatch; not all of `602100` | `c4243af80615172f86509f9b3c4e225da57b2f9b86dc7f01e576cfebf3fe77df` |
| `00512490 / 6` | **Decoded span**, without a recognized Ghidra function | `8afd036ad51eee511dd56af3b01de6b1aabdd3befa65f43710fcaffa41e1f2e7` |

The ten AddressSets are contiguous and their inclusive size matches the
capture exactly; no gap is added. The `60235B` excerpt starts and ends at
instruction boundaries of the retained listing. The six bytes of `512490`
match the Rea raw read: `MOV EAX,FAD8F0 ; RET`. The appearance of this small
body does not turn the span into a function attested by Ghidra.

None of these twelve ranges intersects our five known adaptations, totaling
18 bytes elsewhere: `81EE50/5`, `996850/7`, `97F2C8/2`, `97F668/2`,
`7BEC34/2`. This proves neither global equality of the image nor a general
cause for the differences between memory and the official disk image.

This capture promotes the **presence of the code within these bounds**, not
its use. It did not call the routines, read live texture pointers, inspect
GPU resources, observe pixels or establish an active shader/LOD.
`5050E0`, `505750`, `5121D0`, `512400`, `5F2890`, `5126A0` and the rest of
`602100` in particular are not promoted by these twelve reads.

Closed trial files and recomputed fingerprints:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `code-helpers/observations.json` | 1 917 | `cf86eb0add99ba2cfa960356099bc1787953660c246a3c6acbe675e0d64b8651` |
| `code-helpers/comparisons.json` | 6 258 | `23f9a3683f58de7fa6935f82a0a3073f45be069480eee46c766c08ebc949adae` |
| `startup-report.json` | 2 621 | `66c3615b480c804d368c8f11200b8a3e3a2ef1cee23d524340936000fda594c6` |
| `postflight.json` | 1 984 | `999731889460606043a75d754b9fdf27240c3231beb4d94a3f76c2ca974af107` |

In the private evidence, `verify-runtime23.py --pid 33928 --creation-filetime
134378560639742868` consults files only. Its report
`runtime23-verification.json`, **17 340 bytes**, SHA-256
`1a2ed13c89f9f10fb889ba971650f644eb42d2d4aef734935a9eb7d84d2c4ead`,
keeps the twelve results, the file provenance and the checks.
No commercial binary, asset, snapshot or listing is intended for Git.

## Limits for the reconstruction

The following remain precisely open: the runtime identity of the renderer
behind `FAD8E0`, the order and scope of the rebound banks, slot
substitutions and auxiliaries, parameter types outside the sample,
creation/upload of all formats, shader reflection and keys, the exact uses
of the first eight slot bytes, the LOD policy and the correspondence to the
final rendering. The `.2DM` reader can preserve this data and resolve the
proven equalities; a complete rendering must not be announced from this
boundary alone.
