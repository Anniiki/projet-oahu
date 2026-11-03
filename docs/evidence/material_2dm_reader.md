# C++ reader for `.2DM` materials and primitive binding

## Result and references

`Material2DM` opens the PC v2 subformat described in
[the inventory of three official banks](material_2dm_layout.md).
It links the declared offsets `HASH → MAT → PAR → LAY` without turning
the file's 32-bit words into host pointers. The result owns its data
and compiles for x64 and Win32, C++ latest, `/W4 /WX`.

Material selection follows the official ranges corroborated by
[trial 21](retail_material_bindings.md): `50A480/129`, `40FC80/114`,
`5048A0/114`, `509160/225`. It uses the high WORD of PRIM+28, the
instance's MBNK index and the raw eight-byte key. The MBNK pointer cache
words are never used as addresses, even when non-zero.

## Interface and explicit policies

- `parse(bytes, limits)` checks the six ordered roots, chunks, sizes,
  types, HASH covering the MATs and membership of the references in REAL.
  REAL may be unsorted; duplicates, non-terminal padding and unknown fields
  are refused. Offsets must point at the start of a record of the right type.
- `materials()` keeps the HASH order; `parameters()` and `layers()` keep
  their physical order. `find(key)` uses a separate index, comparing the
  unsigned DWORDs high then low. No truncated name or path enters the lookup.
- Opaque payloads are copied in full, including the file linkage words;
  only documented fields receive an interpretation.
  Classification and flags do not select a shader yet.
- `bind_primitive(scene, bank, word, fallback)` returns a material and its
  source slot. An absent bank, the `FFFF` sentinel or an unknown key
  requires a fallback supplied explicitly by the caller. The fallback's global
  native identity is not established. An out-of-range index is a distinct error.

Null MAT→PAR or PAR→LAY references are representable; aliases share the
index of the declared record. These are safe policies of the port, not
native behaviors validated by this corpus, which has no null or alias.
HASH→MAT remains bijective; duplicate keys are refused as ambiguous.

The six observed PAR sizes are supported. Any other size, a non-empty
MTPA/UVAA or an unknown REAL field produces an explicit subformat
refusal. The reader does not guess a structure inside opaque payloads.
The limits bound the file, the counts, REAL with its padding and the owned
content; temporary indexes stay bounded by these counts. The content budget
excludes reserved capacity, allocator and validation memory.

## Validation

```powershell
cmake --build build --config Release --target material_2dm_test
ctest --test-dir build -C Release -R pc_2dm_material_bindings --output-on-failure
.\build\Release\material_2dm_test.exe "<installation>\Euro\Bnk\Vehicules\Alfa_GT.bnk" "<installation>\Euro\Bnk\Vehicules\300_C.bnk" "<installation>\Euro\Bnk\Vehicules\A3_V6.bnk"
```

Synthetic fixtures: **11 670 checks**, covering truncations, post-NUL and
unsigned keys, reordered HASH, interior or mistyped references, REAL,
alias/null, exact budgets, payload ownership and explicit fallbacks.
The linear search oracle does not share the production sorted index.

With the banks, **12 798 checks** pass per architecture:

| Bank / physical member | MAT/PAR/LAY | OBJ/variant references | Bound primitives | Maximum slot |
| --- | ---: | ---: | ---: | ---: |
| Alfa_GT / 19 | 34/34/34 | 58 | 95 | 11 |
| 300_C / 25 | 37/37/37 | 61 | 95 | 10 |
| A3_V6 / 24 | 37/37/37 | 64 | 99 | 12 |

The **289 primitives** bind without an invented fallback, for every declared
variant. This does not choose which ones to draw. The banks remain
unchanged. Private evidence: `<private-evidence>/reprise-2026-10-30/material-2dm-cpp/`.

This step does not render materials yet. It does not reconstruct all the
parameters, shaders, external textures, transparency, lighting or loading
priorities. The [texture/sampler bindings](retail_texture_bindings.md)
form the next step; no commercial resource is committed.
