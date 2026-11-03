# Selection of type 2 MAT/PAR programs

## Result and level of evidence

Static selection follows the chain **MAT → first PAR → family and variant
descriptor → two program keys → registry → VS/PS objects**. The MAT
classification WORD organizes admission and draw queues; it is not the
program key. The normal path binds the vertex shader, then the pixel
shader, before the PAR constants and the LAY textures.

This investigation establishes neither the program actually active in a
game frame nor its HLSL/DXIL equivalent. It closes input contracts and
static calls, then verifies captures of code/data presence. No OpenTDU
source is used. The documentary verification of trial 24 launches no game
or probe and writes no commercial resource.

Primary reference: the official PC MC 1.66 A executable, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`,
16,797,696 bytes. The Rea analyses below come from the explicitly
identified secondary executable, SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`,
12,263,424 bytes, x86 PE at base `00400000`. **Official trial 24
corroborates the presence of eleven code windows, 1,541 bytes**, equal to
the secondary and different from the official executable on disk at the same VAs. Its
2,048-byte table equals both on-disk executables and the 256 independently generated
entries. These matches are re-verified from the captured files; they
constitute neither an execution trace nor a transitive promotion of the
uncaptured callers, vtables or helpers.

The twelve ranges of official trial 23 remain those of
[retail_texture_bindings.md](retail_texture_bindings.md). They include
`601F90` in full and only `60235B/69` of the dispatcher, **not all of
`602100`**. They were not requested again in trial 24.

## Coordinates and persistent data

`MAT`, `PAR` and `LAY` designate chunk starts, 16-byte header included.
A "payload" begins at `chunk+10h`. Native pointers are x86 DWORDs after
REAL processing; the files keep offsets. See
[material_2dm_layout.md](material_2dm_layout.md) for their typed validation.

| Position in the file | Usage established here |
| --- | --- |
| `MAT+10h`, WORD | Classification, rewritten at setup; distinct from program selection |
| `MAT+12h`, WORD | Number of PAR references; the path studied chooses the first one |
| `MAT+14h`, DWORD | Material flags, also used by admission/render states |
| `MAT+18h`, bit 0 | Chooses one of the two programs of the special branch of `602100` |
| `MAT+60h` and `MAT+80h` | Bases of the native VS and PS pointer caches; no persistent key is derived from them |
| `MAT+E0h`, REAL DWORD | Reference of the first PAR chunk |
| `PAR+10h`, DWORD | PAR type; the present path requires 2 |
| `PAR+18h`, eight bytes | Family key copied to the variant descriptor |
| `PAR+50h`, DWORD | Number of constant descriptors |
| `PAR+54h`, REAL DWORD | Reference of the LAY chunk |
| `PAR+60h` | Start of the constant descriptors, 16-byte stride |

The two cache windows `MAT+60h..7Fh` and `MAT+80h..9Fh` are zero in the
three PAINT_LR payloads examined. `5063E0` writes the chosen slots with an
explicit index; `602100` reads two BYTE indices from the context. The
producers and general bounds of these indices are not closed here. A C++
reader must not interpret these zeros as shader IDs, nor allow an arbitrary
index because the native assembly does not check it.

## Classification, calls and variant preparation

`601D10` receives a MAT chunk and forms its payload. If it exists, the
global callback `FAF474` is called with this payload; AX is written to the
first WORD. Without a callback, the value written is zero. Then `517810`
receives the payload and the addresses of the slots `payload+50h` and
`payload+70h`. It does not use this classification WORD as a program key.

This separation complements the facts in
[retail_material_bindings.md](retail_material_bindings.md): `509160`
filters bit 3 of `MAT_payload+4` and the class through the context mask,
then `5F21B0` files the draw in the classification queues of stride `50h`.
This chooses a draw path without, by itself, identifying the VS or the PS.
The initial loading callback and its current binding remain those
documented there; it is not assumed that every load calls `601D10`.

Observed callers of `602100`: `513970` and `604C00`. The first stores its
MAT argument at `this+1Ch`, then calls the dispatcher. It is called at
`5F1ECC` by `5F1E60`, which also prepares context globals. The second is
another draw path and temporarily modifies bit 3 of the context around the
call. No exclusivity of these paths is claimed.

`517810` builds an **88-byte** buffer on its stack. `603000` zeroes the
22 DWORDs then writes the BYTE `+30h=1`. If the MAT has a PAR, `516F90`
reads its type from the first PAR. For type 2:

1. It reinitializes this buffer and copies `PAR_payload+8/+Ch`, that is
   `PAR_chunk+18/+1Ch`, to the buffer DWORDs `+50/+54h`.
2. `509490` walks the LAY slots, counts the non-zero eight-byte resource
   keys, and supplies the low BYTE value of descriptor `+4`. These are the
   keys `LAY+28h+32*i`, not the first named block of the slot.
3. `5168C0` copies individual BYTEs from the PAR to the descriptor. Exact
   examples: payload `+24→descriptor+14h`, `+25→+15h`, `+26→+16h`,
   `+27→+17h`, `+38..3F→+28..2Fh`. The complete offset table is kept in
   the private batch; no shader function name is assigned to these bytes.
4. Comparisons of family DWORD pairs modify certain bytes. They must be
   preserved to reproduce the variants; this is not a uniform
   "family + texture count" formula. One branch can even rewrite the family
   and PAR bytes, as well as a MAT flag: the native function is not pure.

`5169B0` then produces the two lookups. For the first output, it writes
`descriptor+30h=1`; for the second, `=0`. Each time, `602DD0` passes **the
buffer address and the length `58h`** to `504920`, then the EDX:EAX result
serves as the key into the registry `FADBF0`. The first output is
identified as VS and the second as PS by the constructors/vtables and the
binding methods described below, not by their assumed order.

The **`504920` boundary is now established**: it is a five-byte JMP thunk
to `504980`, a contiguous AddressSet of 117 bytes. Both bodies are present
in official capture 24 and equal to the secondary. A signed count <=0
returns the native seed `0CD5E44AFAC432B1` without reading the input; a
positive count walks exactly that prefix.

The computation uses the table of 256 eight-byte entries at `F465D0`.
Writing L for the native DWORD returned in EAX and H for the one in EDX,
the logical state is `(L<<32)|H`. For each byte, eight MSB reductions with
the logical polynomial `0060034000F0D50B` precede the XOR injection of the
byte into the low eight bits. The result swaps the two words back; there is
no final XOR. The serialized lookup key is LE EAX then LE EDX. See
[resource_hash.md](resource_hash.md) for the C++ contract and its
validation. This identification does not make it a cryptographic function.

The exact 88-byte buffer, including the zeros and the stage BYTE, remains
the input of the caller `602DD0`. A C++ `struct` with implicit padding
does not replace it faithfully. The arithmetic closure does not prove that
a given variant is registered, selected or executed.

`602D00` explicitly compares the two DWORDs of the key and follows the
collisions of a registry. After a bucket is computed by a helper not
analyzed here, each 16-byte node holds the low key at `+0`, the high key
at `+4`, the object value at `+8`, and the next node at `+Ch`. A miss
returns zero.

`517810` does not guarantee that a program exists: after the first
attempt, it walks the decreasing indices from `[F4721C]` for which
`BYTE[FADB48+i]` is active, modifies the BYTE `descriptor+1` and retries.
If one of the two outputs is still missing, it reinitializes the buffer and
tries the fallback family `(3DAFE4F6, B9BAE7F4)` with `descriptor+28h` set
according to the equality of the MAT with the global pointer `F47514`. A
MAT without a PAR starts from this family. The meaning of the index, the
active capabilities and the success of the fallback are not established.
Despite pointer tests on input, the continuation of `517810` dereferences
both outputs: its usable contract requires two valid output addresses.

## Registry, bytecode and binding order

`516C50` reserves two arrays of objects of stride `30h`. Those of `FADBA8`
go through `6029F0`, which installs the vtable `EECF78`; those of `FADBA4`
go through `602870`, which installs `EECE90`. The slots `+8` read from the
vtables are respectively **`602AC0` and `602940`**.

`5165A0` places in each object the low/high key at `+18/+1Ch`, the
metadata pointer at `+8`, calls its virtual slot `+14h` with the first
argument supplied by the registrar, then records the object in `FADBF0`
through `602C80` on success. The detail of slot `+14h` and of the
creators/cache `516380/5163A0` is not established here.

A bounded example proves that this first argument can be embedded
bytecode: `5171A0` pushes `009F2F80`, with the key `(D32BC9DA, 6C6CD946)`,
the metadata `[FADBC8]+60h` and the array selector zero, before `5165A0`.
The 32 bytes read at this VA start with the DWORD `FFFF0200`, then a
COMMENT token and `CTAB`. The D3D9 SDK identifies `FFFF0200` as
`D3DPS_VERSION(2,0)`. **This is not an identification of the PAINT_LR
program**, nor proof that all programs are embedded the same way. No shader
offset in a `.2DM` has been established; this payload contains
selection/constant inputs.

In `602100`, `ESI=MAT_payload`, `[FE77C0]` is the context:

- If `context+24h & 42h` is zero, the BYTEs `context+16Ch` and `+16Dh`
  index the bases `MAT_payload+50h` and `+70h`. Both pointers must be
  non-null. The virtual call of the first at `60234F` precedes that of the
  second at `602356`.
- `602AC0` uses `IDirect3DDevice9::SetVertexShader`, x86 slot `170h`.
  `602940` uses `SetPixelShader`, slot `1ACh`. Each method first installs
  the metadata of its object through `517150`, materializes its handle at
  `object+28h` if necessary, and avoids reapplying the same handle through a
  distinct global cache. If the D3D call fails, it tries to bind zero and
  clears its cache; no successful-rendering diagnostic follows from this.
- If the mask `42h` is non-zero, a distinct branch looks up the keys
  `(DED49E1A,3129566D)` and `(D27AAF0C,444317B1)`, chooses according to
  bit 0 of `MAT_payload+8`, then calls the virtual slot `+8` of the object
  and `513270(0)`. The body of the latter is not analyzed here: the absence
  of a pixel shader in this branch must therefore not be inferred from its
  name or from the zero alone. The object classes of these two keys remain
  to be closed.

Render states are prepared before these programs. Numeric facts of the
dispatcher: bit 3 of the context notably forces D3D9 state 27 to zero,
bit 2 forces state 14 to zero, and the mask `42h` also takes a particular
path for state 168. These numbers correspond respectively to
`ALPHABLENDENABLE`, `ZWRITEENABLE` and `COLORWRITEENABLE` in the SDK.
The gameplay meaning of the passes is not assigned. Other MAT fields are
passed to state helpers that remain open; calling them all "shader flags"
would lose this separation.

## Constants and resources after the choice

The type 2 path of `602100` then calls `601F90`, already corroborated on
the official executable in trial 23. Each 16-byte descriptor holds:

| Offset | Observed native read |
| --- | --- |
| `+0`, DWORD | Logical index of a metadata entry of 16-byte stride |
| `+4`, DWORD | Not used by this loop; remains opaque |
| `+8`, DWORD | Number of data bytes; shifted by four bits for the float4 count |
| `+Ch`, DWORD | Selector: zero chooses the current group, non-zero its subgroup `+30h` |

The current pointer is `[FADBCC]`. `517150` replaces it during a program
binding; its helper `603440` is a RET in the secondary. After the two
normal bindings, it therefore corresponds to the metadata of the **last
bound PS object**, with no proof here that two chosen programs always have
the same group pointer.

The GPU register is not the logical index: the loop consults
`*(group+8) + 16*index`, then the first DWORD of that entry. `FFFFFFFF`
skips the upload but still consumes the data bytes. Finally, **the DWORD
`group+4`**, and not the descriptor selector alone, decides between
`SetVertexShaderConstantF` (`178h`, if zero) and
`SetPixelShaderConstantF` (`1B4h`, otherwise). The initializers of the two
subgroups and the precise names/types of this reflection remain open.

The float4 data start at `PAR+60h+16*N`, then advance by the number of
bytes declared by each descriptor. A safe reconstruction will have to check
overflows, payload and table bounds, 16-byte alignment of sizes and
register limits; the assembly does not provide these guarantees on
inconsistent input.

Constants are uploaded before textures. With the context mask `42h`,
`601F90` skips the texture loop. Outside this mask, the LAY supplies the
BMAP caches and the samplers according to the contract already documented
in [retail_texture_bindings.md](retail_texture_bindings.md). This
investigation adds no semantics to `DETAIL`, `NORMAL`, nor any texture by
name.

## PAINT_LR case based on the official bytes

The raw8 MAT key is `5041494e545f4c52`. The reused positions, physical
members and digests are those of the previous closed inventory:

| Official bank | `.2DM` index | MAT / PAR / LAY in the member | Member SHA-256 |
| --- | --- | --- | --- |
| `Vehicules/Alfa_GT.bnk` | 19 | `63D0 / 35D0 / 0FE0` | `4555bb2b830a08104e17767ed9996450bec1ec430012c898d8b556651f8a6557` |
| `Vehicules/300_C.bnk` | 25 | `73A0 / 44D0 / 18E0` | `8f82f6a38740a84eed3c82c413d53853ed3304c4e5ded6e06d7bde212b004d07` |
| `Vehicules/A3_V6.bnk` | 24 | `7050 / 4010 / 1580` | `bc9fd52b333bd46f291e547c5532bf4706f8c6652f66d49575e7ff268000584d` |

In all three cases, the eight bytes **`PAR+18h` are
`57736c1cb8573118`**, that is low DWORD `1C6C7357` and high DWORD
`183157B8`. This is a family key, not a string derived from "PAINT_LR".
The PAR is of type 2 and measures `1A0h`. The LAY has six slots, two of
which carry non-zero resource keys. The BYTEs copied by `5168C0` are
identical in the three cases. The branch of this family in `516F90` writes
`descriptor+14h = BYTE(+2Dh)+BYTE(+29h)`; if this sum is zero and
`descriptor+9` is non-zero, it writes 1. Here the result is 1.

Thus the inputs of the **first** key computation are identical across
these three materials, with a single byte differing between VS and PS
(`+30h`). The private batch keeps the 88 derived bytes and the non-zero
fields. They define one input of the `504920/504980` computation, now
documented. This proves neither an available registry entry, nor the
fallback index finally chosen, nor the context indices at draw time. The
BYTE transfer by the helpers `509490/5168C0` remains a secondary analysis
not captured by trial 24.

Each contains ten 16-byte constants. The logical indices are
`124,122,123,27,62,130,131,132,213,78`; the DWORD selector is 1 for the
first five and 0 for the other five. The opaque DWORD `+4` is 2. The
descriptors occupy `PAR+60h..FFh`, the data `PAR+100h..19Fh`: they end
exactly at the end of the PAR. The registers and meanings of these values
remain dependent on the metadata of the chosen program.

The reads covered only these three already identified members. The bank
SHAs before and after are equal. No new mass inventory of resources, save
file or account key was read.

## Evidence, scopes and official trial 24 corroboration

Private batch:
`<private-evidence>/reprise-2026-10-30/rea/shader-selection/`.
`evidence-index.json` links all requests, responses and IDs;
`paint-lr-family-inputs.json` contains the metadata of the concrete case;
`runtime-catalog-draft.json` keeps the initial proposal;
`runtime24-verification.json` contains the independent re-verification of
the eleven captures, two PEs, identities and table. The initial proposal is
not rewritten as if it had already contained the new evidence.

| Secondary Rea request and evidence | Decisive fact |
| --- | --- |
| `34`, `ev_2e41c0136224b0588a79423d07bb54ce50f7bee3c69f6f7df58e8af0cd6dd419` | `601D10`, class/caches separation |
| `36`, `ev_79af2fe2fec478ebee24b3b29fa68a2ba471daf22ac52ce79e707d974f14743f` | `516F90`, family and PAR variants |
| `30`, `ev_bd5d9abdad1e71f0fec7fc6c6b0b7be288161e777ae600178909dfd56ea5532b` | `517810`, lookup and fallbacks |
| `23`, `ev_9d10a660fb3d3e3ee92a0f758c519050872c157a1166ff84c11c6595731c941b` | `602DD0`, buffer/88-byte contract to `504920` |
| `14`, `ev_3557303e74d3ec494658e71cbc32471ef23d7fa360a1d1990297cfa60b6baa66` | `5169B0`, separation of the two key computations |
| `04`, `ev_5e4af5f1afcb048917f59e4a03c7718c71406a33b364aee145e87bfeba01a76c` | `602D00`, exact keys and collisions |
| `53`, `ev_a7ccbc5c2add2c50f1c3cf09320be32d2db4877d6d64364326dc17a04f9811b1` | `602AC0`, VS binding |
| `52`, `ev_ee610efedd33c435061059eeaab2dc80fcffa952e2a014986452346aaac756ba` | `602940`, PS binding |
| `45`, `ev_28f941ca8120891770150aec2284d1a79331d2a35c43e72bbc5d681a38d7eb01` | `5168C0`, exact BYTE transfer |
| `46`, `ev_ab261874f515e7d7f09102bc7fe3492aa91c176a436d8a31f5359785bc5784b8` | `509490`, LAY resource key counter |
| `19`, `ev_3ed10952759ebb8242a80544043927c0eece095d3aa541f72c0a977ca6c747f3` | `5165A0`, key/object/metadata registration |
| `42`, `ev_aec4f17b0d63c365ac62009b49b09704b065b7753121baa246572f4429a44cf1` | 32 bytes at `9F2F80`, PS2.0 bytecode header |

The complementary closed batch `rea/shader-variant-hash/session` establishes:

| Secondary Rea evidence | Scope |
| --- | --- |
| `02`, `ev_4136c5443a6a4ffb206dde15423629d80a1ffdd5c92edef1ff11f423cd936353` | AddressSet `504920..504924`, JMP thunk to `504980`, 5 bytes |
| `03`, `ev_30420ae0867779de9763ff921f3ea00757b9c49118bde321d5c6162e3d310b0e` | AddressSet `504980..5049F4`, 117 bytes, contiguous |
| `05`, `ev_e27aa9611e14d83ee9b4ce1409866959a864aa8efc07b37a666210fbe5d4492c` | Complete assembly of the computation |
| `09`, `ev_ac251e8fb01bf1a24bcde846cb5d09fea2c8aead145325bfbd1890095da59b8a` | Table `F465D0/2048` |

The full assembly of `602100` is reused from the closed texture-bindings
batch, evidence
`ev_0d81117de08a1a242a428520f29a9b4013c68d8b311dd93f07d6b600aa2333a7`.
The D3D9 contract comes from the primary declarations of the Windows SDK
`10.0.26100.0/shared/d3d9.h` and `d3d9types.h`; slot offsets are computed
on four-byte x86 pointers.

Ghidra AddressSets obtained explicitly:

- `601D10..601D6A`, 91 bytes, contiguous, evidence `51`.
- `516F90..517140`, 433 bytes, contiguous, evidence `50`.
- `602D00..602D41`, 66 bytes, contiguous, evidence `03`.
- `602100..6024AA`, 939 bytes, contiguous, evidence `49`; only the span
  `6022BD..60235A` is proposed here for the next targeted read.
- `5165A0..516757`, 440 bytes, and `516C50..516EDE`, 655 bytes,
  contiguous, serve as static dependencies; they are not part of the
  proposed minimum.
- The teardown `5163D0` comprises three ranges `5163D0..51644C`,
  `516450..516479`, `516480..5164B1`, that is 217 bytes and a span of 226,
  with gaps of 3 and 6 bytes. It is neither proposed nor presented as
  contiguous.

Trial 24, exclusive profile `assets-shaders`, is kept under
`<isolated-copies>/slot0/20261030T184840-33116/`. PID **30964**, creation
FILETIME **134378597540409344**, base `00400000`, locked official source
SHA identical to the one above: these values match in
`suspended-pid.json`, `startup-report.json`, `observations.json` and
`comparisons.json` according to the available fields. The evidence file
manifest was re-verified. The preflight probe carries the SHA-256
`cf2255447059ef63895599c167d0812f8aeabe8c51fdb5728030a622f1298822`.

| VA / captured bytes | Exact qualification |
| --- | --- |
| `601D10 / 91` | Contiguous AddressSet, MAT initialization |
| `516F90 / 433` | Contiguous AddressSet, type 2 builder |
| `517810 / 309` | Span of decoded instructions, setup/fallbacks |
| `602DD0 / 12` | Span of decoded instructions, key computation call |
| `5169B0 / 96` | Span of decoded instructions, two lookups |
| `602D00 / 66` | Contiguous AddressSet, key comparison |
| `602AC0 / 127` | Span of decoded instructions, VS method |
| `602940 / 127` | Span of decoded instructions, PS method |
| `6022BD / 158` | Chosen span of `602100`, ends at `60235A` |
| `504920 / 5` | Contiguous AddressSet of the thunk |
| `504980 / 117` | Contiguous AddressSet of the computation |

The five decoded spans do not become Ghidra AddressSets merely because their
bytes match. The total is **5 AddressSets, 5 decoded spans, 1 selected
span: 1,541 bytes**. Each window is smaller than 512 bytes. The span
`6022BD/158` does not overlap the dispatch `60235B/69` already
corroborated in trial 23; the rest of `602100` is not promoted.

The eleven `.bin` files were compared directly, at their VAs, against the
two PEs whose SHAs were re-verified: eleven matches with the secondary,
eleven differences with the official executable on disk, with a recount of the differing
bytes. This corroborates the presence of the precise bodies/span in the
identified official process, without attesting their calls, their
arguments or the objects pointed to.

The table was read as **256 eight-byte scalars**, all complete, in
non-executable image pages according to the collector; this is not an
atomic 2,048-byte capture. Its captured bytes equal both PEs and the 256
recomputed entries bit for bit; common SHA-256:
`56bd54524aff39e0c6908390292a97021c2a818bc3ab6fae34ffb72866f54b37`.
Code and table total **3,589 bytes**, without any new read of the twelve
windows of trial 23. This equality is not a guarantee of the table's
future immutability.

The terminal report indicates **100.208 s**, controlled Job timeout,
code 1460, after which the slot was released. The related audits report 6,697
references and 17 source save files unchanged. Their consistency and the
report digests were verified here; this documentary work neither reread
the whole of the data/saves nor opened the process.

The following remain secondary and outside this promotion: the helpers
`509490/5168C0`, the reset `603000`, the constructors/vtables and
registration `516C50/602870/6029F0/EECE90/EECF78/5165A0`, the metadata
management `517150/603440` and their initializers, the bytecode `9F2F80`,
as well as all uncaptured bytes of `602100`. The presence of the VS/PS
methods does not prove their effective binding to a live object/vtable.

A single initial Rea session reused the closed snapshot of the
texture-bindings batch. A request for the vtable slot `602900` was refused
because Ghidra defined no procedure there; this refusal is not an absence
of code. The wrapper exported then closed the session in its `finally`:
`export-finally.json` and `close-finally.json` confirm the bundle and the
snapshot. A complementary session was then opened for the `504920/504980`
boundary and its table alone, reusing the snapshot. Its nine operations
and its `export-finally.json`/`close-finally.json` also confirm export and
closure. The verification of trial 24 opened no Rea session and ran no new
game, probe or build.

## Contract for future native rendering, and limits

A clean reconstruction can keep three distinct identities: raw8 material
key, admission class, then program pair determined by the family and the
exact descriptor. Variants, failures and fallbacks must remain explicit.
Constant tables must be tied to the chosen program pair; neither the
logical index nor the subgroup selector suffices alone to choose a GPU
register/stage. LAY resource keys remain separate from program keys.

Before claiming a faithful HLSL/DXIL translation, the following remain to
be established: the still secondary descriptor-forming helpers, the
registration of the observed PAINT family and its exact VS/PS bytecode,
the corresponding reflection and its initializations, the producers of the
context indices and fallbacks, the special branch `42h` as well as the
other types/families. It will then be necessary to reconstruct the
operations, vertex inputs, coordinate spaces, constants and texture reads
of these programs, then verify them. Merely compiling a generic shader or
choosing a program by the name PAINT_LR closes none of these conditions.
