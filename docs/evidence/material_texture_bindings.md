# Texture bindings and samplers: C++ owner

## Reconstructed scope

`MaterialTextureBindings` reconstructs the per-key resolution of `505060/121`
and the texture/sampler decisions of `601F90/363`. These bodies are
corroborated in the official client during [trial 23](official_trial_23.md).
[The binding evidence](retail_texture_bindings.md) gives the identity of the
two binaries, the AddressSets, SHA and bounds of each range.

This C++ module owns the decoded data and resource identifiers of the x64
client. It contains no D3D9 call, no game pointer and no OpenTDU code. It does
not reconstruct the constants, programs, shaders, GPU uploads or draws of the
whole native function.

## Supported layout and subformat

`from_materials(library, limits)` checks the whole collection: WORD
`MAT+12 = 1`, DWORD `PAR+10 = 2`, PAR/LAY references present and declared.
Other types or counts are refused as unsupported subformats. Offsets include
the 16-byte chunk header.

The count is the **BYTE `LAY+1C`**, limited to the eight physical slots.
Each slot is 32 bytes starting at `LAY+20`: raw key at `+8`, MIN/MAG, MIP, U
and V indices at `+16/+17/+18/+19`. The cache word `+10` and the auxiliary
word `+1C` remain source bytes and never become host addresses. The eight
bytes preceding the key remain opaque.

| Field | Accepted encoding | Result |
| --- | --- | --- |
| MIN/MAG | 0, 1, 2 | LINEAR, POINT, LINEAR |
| MIP | 0, 1, 2 | LINEAR, NONE, LINEAR |
| U/V | 0, 1, 2, 3, 4 | WRAP, WRAP, CLAMP, MIRROR, BORDER |

Other encodings are refused. The MIP3 entry produces ANISOTROPIC in the
native table; it is not silently converted into a valid MIP filter. W is fixed
to CLAMP by the observed route. The border color, maximum anisotropy and sRGB
are not established by these four bytes.

## Caches shared between materials

An owner keeps the MATs in their HASH order and the LAYs in their physical
order. **A single cache exists per LAY**, even when several MATs or PARs share
it. Orphan LAYs remain present, since `505060` walks the LAYA container and
not only the materials in use.

`rebind(loaded)` receives an explicitly loaded collection, in the order
supplied by the caller. Each resource updates every slot with the same raw
key; later matches win. An absence keeps the earlier cache. An all-zero key
can also match an explicitly supplied resource. Tokens are 64-bit identifiers,
zero included, distinct from an absent cache. The host must keep these
resources alive and serialize the operations. Copying an owner clones its
state; the materials of the copied owner still share their LAY caches.

`plan(material_index, global_flags)` exposes the decisions for each ordinal:

- cache present: `bind_cached`, with its token;
- empty cache and non-zero key: `require_fallback`;
- cache and key both zero: `keep_previous` for the texture, with the slot's
  sampler.

The fixed native fallback `FAD8F0` remains unknown; no white texture is created
to replace it. The mask `global_flags & 0x42` suppresses the texture **and**
sampler requests. What these bits actually mean remains unknown.

The port's guards bound element counts, logical memory and loaded resources.
The product of resources × active slots is bounded before any rebinding, by
checked division; an overflow does not leave a partially modified cache.
These budgets and the encoding refusals are not claimed to be native guards.

## Validation and limits

```powershell
cmake --build build --config Release --target material_textures_test
ctest --test-dir build -C Release -R pc_material_texture_bindings --output-on-failure
```

MSVC latest `/W4 /WX`, x64 and Win32: **2,737 synthetic checks**;
**4,773 with the three official banks**. The fixtures notably discriminate
the BYTE followed by `CD`, eight/nine slots, the four indices 0..255, aliases
and orphans, 64-bit/zero tokens, ordered updates and budgets.

| Bank | MAT | Slots | Local `.2DB` resources | Matches | Missing keys | Zero keys keeping state |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Alfa_GT | 34 | 169 | 17 | 27 | 46 | 96 |
| 300_C | 37 | 187 | 23 | 37 | 44 | 106 |
| A3_V6 | 37 | 187 | 22 | 35 | 44 | 108 |

These counts use the `Texture2DBView::resource_key()` keys, without file names
or any invented priority between banks. The banks remain unchanged.
Private evidence: `<private-evidence>/reprise-2026-10-30/material-textures-cpp/`.

The 134 local absences do not prove the absence of global or procedural
resources. The loading scope, releases, original fallback and stage state in
the full renderer remain to be reconstructed. The plan proves neither a
successful GPU upload, nor an active shader, nor faithful in-game appearance.
