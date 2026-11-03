# PC .2DB textures: native view and RGBA8 decoding

## Primary source and limits of evidence

The textures come only from the members of the official banks under
`<installation>\Euro\Bnk`, opened read-only by `BankArchive`. No OpenTDU
code, asset extracted to disk or graphics dependency enters this module. The
C++ code uses the standard library.

Associated installation: official PC MC 1.66 A executable, 16,797,696 bytes,
SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
A Rea session, `original-codecs`, searched for `.2DB` and `BMAP` in this
image: the three `.2DB` occurrences in rdata (`0x00f12f70`, `0x00f15f84`,
`0x00f15f99`) return no references; the raw search for `BMAP` has no result.
Private evidence: `04-search_bytes.json`, `05-search_bytes.json`,
`08-xrefs.json` to `10-xrefs.json` under
`<private-evidence>/reprise-2026-10-30/rea/original-codecs`.
This does not prove the absence of
an original loader. No reconstructed native routine address is claimed for
this module: the current evidence concerns the payloads.

Primary format references, consulted on GitHub and Microsoft Learn:
[TDUMT `_2DB.cs`](https://github.com/djey47/tdumt/blob/e7b433db37df889f4ed99c5907370a75a66fffd1/tdumt-lib/fileformats/graphics/_2DB.cs),
[the ZModeler author's converter notes](https://github.com/djey47/tdumt/blob/e7b433db37df889f4ed99c5907370a75a66fffd1/tdumt-gui/ref/textures/2DB%20-%20oleg.txt),
[BC compression, Microsoft](https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-block-compression)
and [DXT/BC equivalences](https://learn.microsoft.com/en-us/windows/win32/direct3d11/texture-block-compression-in-direct3d-11).
The TDUMT reference that names `0x88` "DXT5-like" is insufficient: the type
DWORD really distinguishes BC2 from BC3 in the official files.

## Version 2 little-endian layout

The header is 80 bytes. All offsets below are relative to the `.2DB` member,
not to the bank that contains it.

| Offset | Observed and used data |
| --- | --- |
| `0x00` | Version DWORD equal to 2 |
| `0x08` | Total size of the member |
| `0x0c`, `0x10` | ASCII identifiers `.2DB`, `BMAP` |
| `0x18`, `0x1c` | Two sizes equal to the total size minus 32 |
| `0x20..0x27` | Raw eight-byte internal key, not guaranteed UTF-8 text |
| `0x28`, `0x2a` | WORD width and height |
| `0x2c` | One for the 2D surface, six for the observed cube cases |
| `0x2e` | Number of stored levels, main image included |
| `0x2f` | Second mip field: equal to the previous one or zero in the accepted cases |
| `0x30` | Format/flags byte, kept raw |
| `0x3c` | Type DWORD determining the pixel format |
| `0x50` | Start of the blocks/pixels |

The other fields remain opaque. In particular, the DWORD `0x40` is not used
as a row pitch: the official values `0x200/0x400` are found on different BC1
dimensions. Pitches are computed from the block size attested by the format.

| Byte `0x30` | Type `0x3c` | Native format | Bytes per unit |
| --- | ---: | --- | ---: |
| `0x84` / `0xc4` | 51 (`0x33`) | BC1 / DXT1 | 8 per 4×4 block |
| `0x88` / `0xc8` | 53 (`0x35`) | BC2 / DXT3 | 16 per 4×4 block |
| `0x88` / `0xc8` | 55 (`0x37`) | BC3 / DXT5 | 16 per 4×4 block |
| `0x90` | 29 (`0x1d`) | BGRA8, bytes B,G,R,A | 4 per pixel |

`0xc4/0xc8` and six square surfaces are the cube cases. The observed sizes
match six complete successive mip chains, without alignment between faces.
The geometric order of the faces remains unknown: slice indices are exposed
as stored, without assigning them `+X/-X/...`.

For each level, the dimensions are halved with a minimum of one. BC uses
`ceil(width/4)` block columns and `ceil(height/4)` block rows; BGRA8 uses the
pixel dimensions. Levels are contiguous, even when a small BC level occupies
only eight bytes. Only the end of the whole set of chains is zero-padded up to
the next multiple of 16. Then comes the fixed footer
`{ "REAL", DWORD(0), DWORD(16), DWORD(16) }`, 16 bytes long.

Real example: `Vehicules/Alfa_GT.bnk`, `lgt_f_g.2db` is 144 bytes: 80 for
the header, 32 for the 8×8 BC1 mip, 8 for the 4×4 mip, 8 for padding, 16 for
`REAL`. The padding belongs to no mip.

## Interface and use with D3D12

`resource_key()` keeps the eight bytes, including after the first NUL.
`name_bytes()` remains a display representation stopped at the first NUL; it
must not be used to link a material to its texture. The tests distinguish two
different binary keys carrying the same display text.

`Texture2DBView::parse(span<const byte>)` returns a borrowed view. The bank
that owns the bytes must stay alive and unchanged. Subresources expose
dimensions, slice/mip indices, `row_pitch`, `slice_pitch` and a bounded view
of the native bytes. Their order is `slice * mip_count + mip`.

```cpp
auto bank = tdur::assets::BankArchive::read_file(bank_path);
// Check bank and select a .2db member index.
auto stored = bank->stored_bytes(member_index);
auto texture = tdur::assets::Texture2DBView::parse(*stored);
// Check texture before conversion.
auto image = texture->decode_rgba8(0, 0);
// image owns its tightly packed R,G,B,A pixels, independently of the bank.
```

The native formats correspond to the DXGI BC1/BC2/BC3 or B8G8R8A8 formats, but their
file pitches are not the D3D12 upload alignments. The GPU loader must obtain
its own footprints and copy the rows. The RGBA8 conversion allows a first
uniform R8G8B8A8 upload without any BC dependency on the viewer side.

The CPU decoder handles RGB565, the BC1 transparent mode, the BC2 four-bit
alphas, the two BC3 alpha palettes and partial blocks at the edge. It keeps
the row order and applies a deterministic integer division for the
interpolations. No gamma/sRGB, vertical flip, premultiplication or normal map
decoding is inferred. Pixel-for-pixel equivalence with the hardware BC decoder
of the original game is not demonstrated; the CPU hashes below are
reproducible reference points, not ground truth for the original
game's visuals.

Errors are typed with offset and message. The default limits are a 256 MiB
file, dimensions of at most 16,384 and 256 MiB per decoded image. Sizes, mip
chains, markers, footer, alignment and bounds of each subresource are checked
before any view is exposed.

## Corpus and explicitly refused cases

The C++ pass through `BankArchive` walked 5,003 banks and 28,790 `.2DB`
members. It accepts 28,765 textures: 19,160 BC1, 307 BC2, 7,645 BC3 and
1,653 BGRA8. The version 2 corpus contains 259 cubes. The largest observed
dimensions are 2,048×2,048, with ten levels at most.

The 25 refusals are kept as unknowns, without replacement data:

- Eight big-endian headers, in the gauge banks
  `FrontEnd/HiRes/Gauges/Zonda_C.bnk` and
  `FrontEnd/LowRes/Gauges/Koen_CC8.bnk`: outside the supported PC LE layout.
- Three version 1 headers, including `Vehicules/Rim/Audi/A3_F_01.bnk` and
  `A6_F_01.bnk`, then `Vehicules/Rim/Mercedes/SLK_350_F_01.bnk`: their
  layout differs and is not forced onto the 80-byte header.
- Twelve textures in `scenarics/Packs/worldactors.bnk`, including `v_halo_2`,
  `sf_dbinc`, `feux_tricolores`: two diverging non-zero mip fields and a
  payload size incompatible with the usual 2D chain. Animation, atlas or any
  other interpretation remains an unimplemented hypothesis.
- Two type29 members with byte `0x10`, in
  `Vehicules/Rim/Ducati/Duca999R_F_01.bnk` and `Duca999R_R_01.bnk`: this
  variant also has extra bytes and remains undecoded.

Fourteen other textures have a zero second mip field while having a
consistent first field and size; twelve of these textures are accepted, the
two Ducati variants remain refused for their format.

## Executed checks

Direct MSVC x64 compilation succeeds with
`/std:c++latest /W4 /WX /permissive- /utf-8`, without any external dependency.
The standalone test passes **306 checks**: known BC1 pixels, transparency,
equal endpoints, RGB565 expansion, BC2 alpha, both BC3 branches and their
48-bit indices, BGRA/RGBA swizzle, small mips, block edge and slices. The
expected colors and alphas are constants independent of the decoder. It also
covers truncations, counters, dimensions, size multiplication, markers,
footer, padding, budgets and out-of-range indices.

The structure pass over all banks passes **204,233 checks**, with 28,765
accepted textures and the 25 refusals above. It does not decode the pixels of
the whole corpus. Four banks (`Alfa_GT`, `DEF_BOT`, `GPH_L1A`, `Birds`) pass
**1,148 checks**: 62 textures, 388 subresources and 13,602,688 decoded RGBA
bytes, without writing any asset.

| Bank / member | Format and dimensions | Mips / slices | FNV1a64 of the RGBA pixels of level 0, slice 0 |
| --- | --- | --- | --- |
| `Alfa_GT` / `body_lr.2db` | BC3, 256×256 | 7 / 1 | `bc17b2992d0d7a68` |
| `Alfa_GT` / `lgt_f_g.2db` | BC1, 8×8 | 2 / 1 | `e613e43582ac02a5` |
| `Alfa_GT` / `ws_logo.2db` | BGRA8, 128×128 | 6 / 1 | `d17c6bc1cfa3e6f5` |
| `DEF_BOT` / `couture.2db` | BC2, 256×256 | 7 / 1 | `b598a5d97f24bbff` |
| `GPH_L1A` / `cubecar.2db` | BC1, 256×256 | 7 / 6 | `97cbab6f09c3aab3` |

Validation commands with the standalone test binary:

```powershell
.\build\texture-local\texture_2db_test.exe
.\build\texture-local\texture_2db_test.exe --official-root '<installation>\Euro\Bnk'
.\build\texture-local\texture_2db_test.exe '<installation>\Euro\Bnk\Vehicules\Alfa_GT.bnk' '<installation>\Euro\Bnk\Avatar\DEF_BOT.bnk' '<installation>\Euro\Bnk\Interior\GPH_L1A.bnk' '<installation>\Euro\Bnk\Details\Birds.bnk'
```

Local logs: `build/texture-local/official-corpus.log` and
`official-samples.log`. No game trial or scene rendering validation is
implied by these results.
