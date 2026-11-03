# PC material bindings: 3DG, 3DD and 2DM

## Result and evidence levels

The path studied establishes a precise binding: the WORD `PRIM+0x2A`
selects an entry of the instance's MBNK; its raw eight-byte key is resolved
in the material HASH of the `.2DM`. The instance's OBJ reference list is also
established: an unsigned16 count at `INS+0x5E`, 20-byte entries from
`INS+0x60`. This replaces the assumption of a single object per instance; no
dynamic variant selection is reconstructed.

The evidence levels remain distinct:

- **Official assets**: structures and references verified read-only in
  `Alfa_GT.bnk`, `300_C.bnk` and `A3_V6.bnk`, reusing
  [scene_3dd.md](scene_3dd.md) and [geometry_3dg.md](geometry_3dg.md).
- **Secondary routines**: readable instructions of the Platinum/Paradise
  executable, SHA-256
  `f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
  The eleven bodies exactly compared in this batch **all differ from the
  official disk** MC 1.66 A, SHA-256
  `3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
- **Official corroboration of the code in memory**: the closed trial 21 finds
  the seven ranges of the binding/list core, 1 179 bytes, exactly identical
  to the secondary and different from the official disk. Five cover complete
  AddressSets; two remain selected spans, detailed below. This presence does
  not prove their execution. The other routines cited keep their earlier
  level; the old unreadable original readings of `50AD00/504930` do not
  become evidence of the parser.

A single Rea session, resumed from the existing secondary snapshot, performed
84 operations; bundle and snapshot were exported, then the session was closed.
No OpenTDU code is reused. The static analysis and then the verification of
the trial 21 files launched no game, process probe or build; the trial was
conducted separately. No license/decryption key or
account data is used. The bindings serve the future x64 C++ client with
direct D3D12/Vulkan; they do not constitute a complete renderer.

## From instance to material

All code addresses below are x86 VAs, base `00400000`. Asset offsets include
the 16-byte header of the named chunk.

1. `50A480` receives the 3D collection in ECX and the material collection on
   the stack. It walks the consecutive MBNKs under the container referenced
   by `collection3D+0x0C`. One MBNK entry is 16 bytes, starting at `MBNK+0x10`.
2. The two key DWORDs are passed to `40FC80`. That routine searches the HASH
   of the MATA container referenced by `materialCollection+0x0C`, using
   `5048A0`. The comparison uses the two **unsigned** DWORDs, first the one
   at `key+4`, then the one at `key+0`: no string comparison.
3. On a non-null result, `50A4E4` writes the MAT chunk pointer into
   `mbnkEntry+8`. A failed lookup leaves this field unchanged; it does not
   clear it. The new independent reader can keep this resolution in a typed
   table, without modifying the source bytes.
4. `50E350..50E357` copies the field `INS+0x54` to `instance_runtime+0x0C`.
   Its first argument is `INS+0x10`, passed in particular to `50AC5C` from
   `50AB40`; the instruction therefore reads `[argument+0x44]`.
5. In `509160`, the first PRIM is taken from `geometry_runtime+8` and the
   primitive count from `+0x0C`. At `5091B2`, `MOV AX,[PRIM+2A]` reads the
   unsigned16 selector, that is bits 16 to 31 of the DWORD kept by the
   reader under `opaque_binding_word`.

For an ordinary index, the instructions `5091C1..5091CC` compute:

```text
slot = uint16(PRIM[+0x2A])
MAT_chunk = *(instance_runtime.MBNK + 0x18 + 0x10 * slot)
MAT_payload = MAT_chunk + 0x10
```

The computation accounts for the MBNK header and the cache field at
`entry+8`. It is neither a direct offset into the `.2DM` nor a global
identifier. The low sixteen bits of `PRIM+0x28` are not used by this
selection; their general meaning remains open.

### Fallbacks and filtering actually observed

A missing MBNK, the selector **`FFFF`**, or a null material pointer trigger
the fallback to the global pointer `F47514` at `5091D6`. No index must be
created for the sentinel. The path checks neither `slot < mbnkKeyCount`
nor an equality between the key count and the primitive count.

After resolution, bit 3 of the DWORD `MAT_payload+4` must be clear and the
bit corresponding to the WORD `MAT_payload+0` must be admitted by the
context's mask64. The call `509221 -> 5F21B0` then queues the draw with
material, instance and `PRIM+0x10`. This does not prove which shader will
execute. `5092F0` repeats the resolution/fallback on a path limited to the
first PRIM, with a different admission check; do not present `509160` as
the whole of the engine's rendering.

Corrupted buffers do not become valid because the native code omits guards.
The port may refuse an index outside the MBNK and explicitly require an
available fallback material; no unknown material becomes a silent success.
Nor is the native lookup a bounds validator for the HASH: the port works on
its declared, bounded list.

## Instance entry list: exact contract

`50E330` receives the destination instance in ECX and three arguments on the
stack; it ends with `RET 0C`. Its first argument points to `INS+0x10`.
The assembly, rather than the `float*` types of the pseudocode, establishes:

| Source field | Instruction / handling | Runtime destination |
| --- | --- | --- |
| `INS+0x54` | `50E350`, DWORD read | `+0x0C`, shared MBNK reference |
| `INS+0x5E` | `50E365`, **unsigned WORD** read | `+0x5C` |
| `INS+0x60 + 20*i` | Pointer to a HASH OBJ entry, DWORD | `+0x88 + 32*i` |
| Entry `+4,+8,+0xC,+0x10` | Four DWORDs copied without interpretation | `+0x98,+0x9C,+0xA0,+0xA4 + 32*i` |

`50E36F` compares the count with zero; `50E39A` skips the loop when it is
zero. `50E3E4` reloads it with `MOVZX`, then compares it with the loop
counter at `50E3F4`. No `int16_t`, no stop at an HR/LR name and no invented
zero entry appears. With a zero count, **no list entry is copied**; the
other fields and the matrix still follow their own initialization.

The routine does not check the count against the record size. The official
INS examined are `0xB0` bytes: exactly four 20-byte slots remain after
`+0x60`. The safe bound for this layout is therefore
`count <= (record_size - 0x60) / 20`, that is four, after checking the
minimum size. This bound comes from the observed format, not from a native
guard.

In the three banks, each reference used at `INS+0x60+20*i` appears in
**REAL** and targets exactly one HASH OBJ entry. The target field
`HASH_OBJ+8` also appears in REAL and targets OBJ. The four descriptor
DWORDs do not appear in REAL, across the 183 entries checked. The non-null
reference `INS+0x54` appears there separately. Relocations precede native
use; `506070` adds the base to the fields declared by REAL. The x64 reader
keeps typed offsets/indices, without writing a 64-bit pointer into these
32-bit file fields.

The MBNK belongs to the instance and remains shared by its variants. Its
presence is **optional on the native rendering path**, which has the
fallback described above. No native test enforces
`MBNK.keys.size() == premierOBJ.primitive_count` or the same equality for all
alternate OBJs. For a bounded port, the indices actually used by each chosen
PRIM are what must be validated.

## Independent inventory of the three banks

Distinct objects are counted per OBJ record actually referenced, without
deduplicating displayed strings. No dynamic choice is applied.

| Bank | HIE / INS nodes | Nodes with entries | Sum of counts | Distinct OBJ, entry0 / all | Counts 0 / 1 / 2 |
| --- | ---: | ---: | ---: | ---: | --- |
| Alfa_GT | 97 / 97 | 56 | **58** | 33 / **34** | 41 / 54 / 2 |
| 300_C | 101 / 101 | 59 | **61** | 35 / **36** | 42 / 57 / 2 |
| A3_V6 | 103 / 103 | 62 | **64** | 34 / **35** | 41 / 60 / 2 |

The earlier figures 56/59/62 are therefore counts of non-empty nodes, not
exhaustive entry counts. All entries together give
**183 OBJ references and 289 primitive bindings**: 95/95/99 per bank.
In this sample, each variant has as many primitives as its key MBNK and all
ordinary selectors are within bounds; this is an observation of these files,
not a native constraint to generalize.
The 124 zero-count instances here have a null MBNK and a null first slot.

### `RV_L` examples, member-relative offsets

The two variants of each mirror have three primitives and share their
three-key MBNK. The full keys are
`RV_HR\0\0\0` and `RV_LR\0\0\0`, that is
`52565f4852000000` and `52565f4c52000000` respectively.

| Bank; INS / MBNK | Entry0: position; HASH → OBJ → GEOM | Entry1: position; HASH → OBJ → GEOM |
| --- | --- | --- |
| Alfa_GT; `2F10 / 0230` | `2F70`; `0660 → 0AD0 → 13D0`, RV_HR | `2F84`; `0670 → 0AA0 → 11B0`, RV_LR |
| 300_C; `24F0 / 0340` | `2550`; `0670 → 0BA0 → 1DD0`, RV_HR | `2564`; `0680 → 0B70 → 1BB0`, RV_LR |
| A3_V6; `3490 / 03E0` | `34F0`; `0670 → 0C10 → 22F0`, **RV_LR** | `3504`; `0660 → 0BE0 → 20D0`, **RV_HR** |

The first descriptor is `[41A00000,0,0,0]` in all three cases. The second
is `[43C80000,0,0,0]` for Alfa_GT/300_C and `[43FA0000,0,0,0]` for A3_V6.
The first values correspond to the float32 values 20, 400 and 500; their
units and dynamic use are not established. **A3_V6 inverts the suffixes**:
an HR/LR sort would therefore produce an order different from the file.

The reusable private JSON `rv-l-variants.json` keeps, for these six entries,
the 20 raw bytes, all offsets, the keys, the REAL positions, the MBNKs and
the inventories above. `variant-bindings.json` contains the 301 INS.
A host may offer an explicitly chosen entry index, refuse an index beyond
the count and expose the ambiguity; no distance policy is derived from the
suffix or from the descriptor values alone.

## From MAT to parameters, layers and resources: limit reached

In the three PC version 2 `.2DM` files examined, MATA has a HASH of
eight-byte keys to MAT chunks. The field **`MAT+0xE0` references PAR**,
then **`PAR+0x54` references LAY**; these fields appear in REAL and target
the starts of declared chunks. They are chunk bases, headers included.
The three files give 34, 37 and 37 materials respectively.

Official Alfa_GT example: `BODY_HR`, INS `3120`, MBNK `02B0`, first
PRIM `1780`, selector zero. The MBNK key is `GLASS\0\0\0`; MATA.HASH targets
MAT `73C0`, its `+E0` targets PAR `4FB0`, then PAR `+54` targets LAY `2300`.
The link is exact, with no guessed file-name association.

The LAYs contain 32-byte slots, with words resembling `DETAIL`, `NORMAL`
and similar semantics, and other eight-byte keys.
This observation is not enough to provide the native contract for `.2DB`
texture lookup, samplers, color spaces or shaders. These fields are not
implicitly converted into file paths.

The dispatcher `504B10`, decoded over a bounded 92-byte span, stores MATA at
`collection+0x0C` and calls the virtual slot `+0x38` of the global object
`FAD8E0` for a MAT chunk. The nominal setup of this global and a vtable are
readable, but no concrete binding of this operation to the loading of a
texture or shader program is established here. In `5F21B0`, the first
WORD of the payload indexes a classification table with a stride of `0x50`
and organizes the draw queues; **this is not a proven shader identity**.
The final shader, its parameters and the pass variants remain to be
established before reproducing the game's appearance in D3D12/Vulkan.

## Private provenance and corroboration catalog

Batch: `<private-evidence>/reprise-2026-10-30/rea/material-bindings/`.
`evidence-index.json`, `disk-comparisons.json`, `finding-ledger.json` and the
asset reports separate facts, interpretations and unknowns. The bank
fingerprints are rechecked, without writing their payloads:

| Bank | SHA-256 |
| --- | --- |
| Alfa_GT | `a049fb59b554d41130e4297fd4ad0c65cc91352052b07fde167329cf1a09e912` |
| 300_C | `461e1543c8cee40aa2ff8880441c4bfcc08da4047507505ae5f09981c0ce10e9` |
| A3_V6 | `290794c9e5f2a68ac236120957f68900518f9185b0318b19015cdd23d4d4e72e` |

Main assembly evidence:

- `41`, list copy `50E330`:
  `ev_d563fefddc3d8031efd6a87f994d9984e26ba8dd569056acf9148fc80f9c0803`;
- `53`, selection `509160`:
  `ev_09a4f730363f758206996f59d22bac7deed71723d752d7e8a5e571a29f774000`;
- `27`, MBNK resolution `50A480`:
  `ev_ffdeab9f29677017beb162d93002055e24fd06b9d8c28bd4f7dd2b37b68d414f`;
- `32`, MATA lookup `40FC80`:
  `ev_0eab4e7e58425b4fda3d560435adac77696114ec8ad94ca7e3d987ac54f97f49`.

The calling constructor appears in the retained folder
`ev_e03e8aa22cd8a4f4958f90f5ae2c2d45aa2b7b595607ca989317277aa7c3a42a`.
The dispatcher, not recognized as a function by Ghidra, is supported by
reading `46` and `secondary-504b10.asm.txt` from the MSVC decoder; this
separate listing is not presented as a Rea AddressSet.

`corroboration-proposal.json` proposed, for a trial already justified
independently, seven small executable readings totaling
**1 179 bytes**: `50E330/340`, `50A480/129`, `40FC80/114`, `5048A0/114`,
`509160/225`, dispatcher `504B10/92`, caller excerpt `50ABBC/165`.
The last two are selected spans, not attested complete function bodies.
Each reading stays under 512 bytes. This catalog describes a presence to
corroborate within these bounds, without proving an execution or a
LOD/shader policy. No asset, binary, snapshot or commercial listing is added
to the repository; only the documentary results are intended for it.

These seven fixed readings were then integrated into the
opt-in `native-adapter` profile, together with other additions prepared
separately. The catalog of **84 ranges / 18 503 bytes** was read during
trial 21; the integration alone is not the proof: the observed files are
verified below.

## Trial 21: seven bounded official matches

Closed batch: `<isolated-copies>/slot0/20261030T164158-32976/`.
PID **2692**, creation FILETIME **`134378521269961313`**, identical in
`suspended-pid.json`, `startup-report.json` and `code-helpers/observations.json`.
The observed base is `00400000`. The official file is verified by its
SHA-256 and locked. Requested duration 120 s, 120.171 s reported; the
process owned by the Job was terminated at the deadline, code 1460. The
postflight records the released slot, the 6 697 unchanged reference files
and the 17 save files whose before/after fingerprints match.

The probe reports **84/84 readable ranges, 18 503 bytes**. The independent
recheck covers the following seven material captures: size and SHA of the
`.bin` files, byte-by-byte comparison with the `.text` sections of the two
PEs whose complete fingerprints are rechecked, then a bounds check against
the retained Rea evidence. The difference listing of the review report
is also recomputed for these seven ranges.

| VA / bytes | Corroborated coverage | Observed SHA-256, equal to the secondary |
| --- | --- | --- |
| `0050E330 / 340` | Complete AddressSet: INS entry copy | `c522d32720b88ebaf60f2d1b780c65e064b0ea0bb218fb6103fbac4582711bc2` |
| `0050A480 / 129` | Complete AddressSet: MBNK key binding | `b4683f2b649022725a0c1a5f8b3716ed8e8d126ebcac934924461509ace5b3cb` |
| `0040FC80 / 114` | Complete AddressSet: lookup in MATA.HASH | `cd86cce4f0fd47a78ebe4284215fa002546614e2fb0707d8a62463fd912db300` |
| `005048A0 / 114` | Complete AddressSet: raw key comparison | `d97b4e0748cce06c60f2df2b9c5ef5dd89d95a4604459b2b3e48fe088be3a835` |
| `00509160 / 225` | Complete AddressSet: PRIM selector and fallbacks | `164c4c3d631409ca3a422a8ef479e569753fabaf955df2d5fac65b067171e3ec` |
| `00504B10 / 92` | **Selected span** of the dispatcher, not a complete Ghidra body | `21f209e20404a52cfde8f9b07c9b4c817d599ddae49205eb5b305d1d596445d8` |
| `0050ABBC / 165` | **Selected span** of the call site, not all of `50AB40` | `842f844f2f18d5b1bdfb9a31b365cddf3eb2e36a16e2081c09cb0888b114066e` |

The first five sizes cover exactly the five contiguous AddressSets announced
by `resolve_containing_procedure`, with no missing prefix or filled gap.
The dispatcher matches the first 92 bytes of Rea reading `46`; the caller
excerpt stays inside the retained body of `50AB40` and starts/ends at
instruction boundaries. The equality of these two excerpts does not turn
their coverage into proof of a complete function.

The seven captures are **all different from the official PE on disk**, with
no edit of our own in these ranges. The 18 bytes of our five known
adaptations are elsewhere: `81EE50/5`, `996850/7`, `97F2C8/2`, `97F668/2`,
`7BEC34/2`. Their absence from the seven new ranges is verified; no global
equality of the image nor any cause of the disk/memory differences is
inferred from this.

This corroboration supports the static list contract, the MBNK indexing and
the documented fallbacks **within these bounds**. It does not observe their
call, the content of a live instance, a material actually drawn or the
validity of the renderer's virtual bindings. It provides no new rule for the
dynamic choice of LOD, texture or shader. `5092F0`, `506070` and `5F21B0`,
in particular, are not promoted by these seven readings.

### Evidence files and private verifier

The fingerprints below identify the closed files of the trial:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `code-helpers/observations.json` | 11 065 | `18aad5de1cc7d8d1b5b763d98f3e2f22fdce19da4ca4d8bb77477b5e2f3e63cf` |
| `code-helpers/comparisons.json` | 226 271 | `b4eee9b2aa55391936d6a93824ac8f7ed9dc359fd6909fd9650c2713e0de5678` |
| `startup-report.json` | 2 617 | `7aba4778e6ef8ff184aad6003c11624f5a7148656fdf055ea0d248e3ba45c542` |
| `postflight.json` | 1 984 | `999731889460606043a75d754b9fdf27240c3231beb4d94a3f76c2ca974af107` |

In the private `material-bindings` batch, `verify-runtime21.py` reads only
these files, the seven raw captures, the local PEs and the already closed
Rea exports. `runtime21-verification.json` keeps the sources/SHAs, the
bounds, the coverage classifications and the seven recomputed results.
It opens no process, executes no target and modifies no probe. The private
report is 13 321 bytes, SHA-256
`9b281c85e18157b1c3a232ae6bddb3a5fe4d6f280eb24e158866daa6fc2e02bb`.
