# PC KNAB bank reader

## Scope and provenance

The standalone C++ reader opens the official banks read-only, preserves
their bytes and exposes the members for the engine's future loaders. It
reuses no OpenTDU code. The internal formats `.db`, `.2db`, `.3dg`, etc.
remain opaque payloads; copying their contents does not mean decoding them.
No commercial file is added to the repository.

Primary format references consulted on GitHub:
[TDUMT `BNK.Sections.cs`](https://github.com/djey47/tdumt/blob/e7b433db37df889f4ed99c5907370a75a66fffd1/tdumt-lib/fileformats/banks/BNK.Sections.cs)
and [TDUMT `BNK.FileSystem.cs`](https://github.com/djey47/tdumt/blob/e7b433db37df889f4ed99c5907370a75a66fffd1/tdumt-lib/fileformats/banks/BNK.FileSystem.cs).
TDUF itself delegates this format to TDUMT. The code of this module is
rewritten from the observed structures, then verified against the local files.

The reference installation is `<installation>\Euro\Bnk`.
The associated official PC MC 1.66 A executable is 16,797,696 bytes, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The exported routine `0x009abc30` names `DB.bnk`, but its read calls are not
sufficient evidence of the parser. This module therefore does not claim an
attested reconstruction of that original routine.

## Verified layout

All integers are little-endian. The 5,003 `*.bnk` files examined have
`40 00 00 00` at `+0` and `4b 4e 41 42` (`KNAB`) at `+8`.

| Absolute offset | Observed field |
| --- | --- |
| `0x00`, `0x04` | Header section size 64, checksum |
| `0x18` | Declared physical size of the bank |
| `0x28`, `0x2c` | Member count, recorded year |
| `0x30..0x3c` | Offsets of the record, optional type, name and permutation sections |
| `0x44` | Start of the payloads; zero in the five empty banks |

Each metadata section carries a `{u32 taille, u32 somme}` prefix (size,
checksum). The observed checksum is the complement of the public zlib
`crc32()`, computed over the section body without prefix or alignment. Each record is
16 bytes: payload offset and size, then two opaque DWORDs. A terminal record
with zero size and zero DWORDs marks padding; it is not a resource. The two
WORDs of the type table remain opaque and are kept by physical record index.

The compact tree identifies a directory by a marker `>=0xc0`; its length is
`256-marqueur` (256 minus the marker), followed by the one-byte child count
and its name. An additional byte `<=0x1f` may precede the name; its meaning
remains unknown. The 19 official occurrences observed are `0x01`. Following
TDUMT, it is ignored: interpreting it as the high byte of the counter
overflows the tree of `CommonWorld.bnk`. The name length of leaves is their
marker; their parent is an extension group. `inplane.bnk` contains two
successive roots. The section ends with zero.

The permutation maps leaves to physical records. Its indices are one byte up
to 254 members, then two bytes from 255 members, because the terminal record
counts toward the 255-record threshold. Names never determine a destination
on disk.

## Contract and protections

`BankArchive::read_file()` only requires a read stream; `parse()` takes
ownership of a vector. `entries()` follows the physical records; `path`
provides a normal name and `tree_path` keeps the extension group.
`stored_bytes()` returns a view tied to the archive's lifetime; `extract()`
returns an independent in-memory copy. Payloads are never interpreted.

The reader refuses incorrect checksums, overlapping sections, repeated or
out-of-range indices, overlapping payloads, truncated trees, duplicate names
and inconsistent sentinels. Errors carry a code, an offset and a message. The
configurable limits cover bank/name bytes, member count, depth and path
length. Defaults: 256 MiB/16 MiB, 100,000 members, 128 levels, 4,096 bytes
per path. These limits and explicit refusals are protections of the new
reader; they do not claim to reproduce the tolerance policy of the original
game.

## ZC and limits

No `.zc` file was found in this copy and none of the 5,003 banks has an
external ZC wrapper. This first reader therefore does not integrate zlib and
returns `unsupported_format` on such entries. ZC is not validated by an
artificial fixture. If real evidence requires this format, the proposed
dependency is [upstream zlib 1.3.2](https://zlib.net/), tar.gz archive SHA-256
`bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`.

## Validation

The C++ tests build synthetic banks, without game data. They cover inverted
permutation, two roots, the extra control byte, empty banks, zero-size members,
a missing optional type section, the 254/255 threshold, views/copies and owner move; then
truncations, checksums, bounds, overlaps, sentinel, trees, indices and
budgets. The optional test arguments are local banks, each extraction of
which is compared with the stored range.

`tdur_inspect_bank --bank <file> --verify-extractions` lists the members and
copies each one in memory, without writing resources. The test also accepts
`--official-root "<installation>\Euro\Bnk"` to walk the local banks. Tests
without arguments remain self-contained and free of commercial data. No
in-game trial is involved.

Direct MSVC x64 compilation succeeded with `/std:c++latest /W4 /WX
/permissive- /utf-8`, without external dependency. The self-contained test
passes its 289 checks. The full run of the C++ test validated 5,003 banks and
87,706 members, with 93,000 checks and zero failures. It includes the five
empty banks, the forest of `inplane.bnk`, the
16-bit indices and the opaque control byte of `CommonWorld.bnk`. The tests compare
each extraction with the bounded view of its stored bytes; they do not claim
to verify the meaning of the internal payloads. Three checks of known
synthetic content independently verify range selection.

## Two reproducible official inventories

`DataBase/DB.bnk`: 624,640 bytes, year 2007, SHA-256
`9e8f656dd95ba32431f57ba2e6557fb347fabbacec43b27ce92633703f82bebb`.
The inspector extracted its 18 members in memory (574,112 stored bytes).
The members share an internal directory named `Db_encrypted`; that name and
the opaque bytes are observed, and this reader undertakes no decryption.

| Physical record | Name | Offset | Bytes |
| --- | --- | ---: | ---: |
| 0 | `TDU_CarPhysicsData.db` | 816 | 107288 |
| 1 | `TDU_Menus.db` | 108112 | 24816 |
| 2 | `TDU_Rims.db` | 132928 | 20352 |
| 3 | `TDU_CarRims.db` | 153280 | 6256 |
| 4 | `TDU_Interior.db` | 159536 | 29112 |
| 5 | `TDU_CarColors.db` | 188656 | 271720 |
| 6 | `TDU_Brands.db` | 460384 | 2904 |
| 7 | `TDU_Houses.db` | 463296 | 4480 |
| 8 | `TDU_CarShops.db` | 467776 | 17520 |
| 9 | `TDU_Clothes.db` | 485296 | 22816 |
| 10 | `TDU_Hair.db` | 508112 | 264 |
| 11 | `TDU_Tutorials.db` | 508384 | 3464 |
| 12 | `TDU_AfterMarketPacks.db` | 511856 | 5720 |
| 13 | `TDU_CarPacks.db` | 517584 | 22688 |
| 14 | `TDU_Bots.db` | 540272 | 4488 |
| 15 | `TDU_Achievements.db` | 544768 | 5048 |
| 16 | `TDU_PNJ.db` | 549824 | 24960 |
| 17 | `TDU_SubTitles.db` | 574784 | 216 |

`Vehicules/Alfa_GT.bnk`: 1,646,592 bytes, year 2006, SHA-256
`a049fb59b554d41130e4297fd4ad0c65cc91352052b07fde167329cf1a09e912`.
Its 21 members were copied in memory (1,644,024 stored bytes). The `.2db`
entries are under a `maps/` directory.

| Physical record | Name | Offset | Bytes |
| --- | --- | ---: | ---: |
| 0 | `Alfa_GT.3DD` | 928 | 29280 |
| 1 | `Alfa_GT.3DG` | 30208 | 473520 |
| 2 | `retro_g.2db` | 503728 | 5552 |
| 3 | `cock_lr.2db` | 509280 | 174864 |
| 4 | `body_lr.2db` | 684144 | 87472 |
| 5 | `lgt_rof.2db` | 771616 | 21936 |
| 6 | `lgt_r_g.2db` | 793552 | 21936 |
| 7 | `lgt_ron.2db` | 815488 | 21936 |
| 8 | `lgt_fof.2db` | 837424 | 21936 |
| 9 | `ws_lgt_f.2db` | 859360 | 174816 |
| 10 | `lgt_f_g.2db` | 1034176 | 144 |
| 11 | `lgt_fon.2db` | 1034320 | 21936 |
| 12 | `ws_logo.2db` | 1056256 | 87456 |
| 13 | `details.2db` | 1143712 | 87472 |
| 14 | `glass.2db` | 1231184 | 174848 |
| 15 | `glass_g.2db` | 1406032 | 87472 |
| 16 | `ws_exhaust.2db` | 1493504 | 87456 |
| 17 | `exhaust_a.2db` | 1580960 | 11024 |
| 18 | `logo.2db` | 1591984 | 21936 |
| 19 | `Alfa_GT.2DM` | 1613920 | 30816 |
| 20 | `Alfa_GT.btrq` | 1644736 | 216 |

Payload bytes are not reproduced here; these observations identify the
members and do not constitute a parser for the formats.
