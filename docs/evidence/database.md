# Original PC databases: resource reading and driving parameters

## Provenance

The members come from the official banks `DataBase/DB.bnk` and
`DataBase/DB_FR.bnk`, opened read-only by `BankArchive`. All decoding stays
in RAM; no plaintext file, asset, save or game constant is added to the
repository. The C++ implementation reuses no OpenTDU game code and depends
only on the standard library.

The associated MC1.66A executable is 16,797,696 bytes, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The `DB.bnk` bank is 624,640 bytes, SHA-256
`9e8f656dd95ba32431f57ba2e6557fb347fabbacec43b27ce92633703f82bebb`.

Primary references consulted on GitHub:
[TDUMT XTEAEncryption](https://github.com/djey47/tdumt/blob/e7b433db37df889f4ed99c5907370a75a66fffd1/tdumt-lib/support/encryption/XTEAEncryption.cs),
[DB.cs](https://github.com/djey47/tdumt/blob/e7b433db37df889f4ed99c5907370a75a66fffd1/tdumt-lib/fileformats/database/DB.cs),
[DBResource.cs](https://github.com/djey47/tdumt/blob/e7b433db37df889f4ed99c5907370a75a66fffd1/tdumt-lib/fileformats/database/DBResource.cs),
[TDUMT physics fields](https://github.com/djey47/tdumt/blob/e7b433db37df889f4ed99c5907370a75a66fffd1/tdumt-gui/gui/wizards/vehiclemanager/common/SharedConstants.cs),
[TDUF CarPhysicsData schema](https://github.com/djey47/tduf/blob/f80b01251d60b34e8ef4c43f51492827a9e8b491/lib-testing/src/main/resources/db-json/TDU_CarPhysicsData.structure.json).
The names and values read from the local files remain the reference for
the tested copy; the public schemas do not replace the data.

## Resource encoding and installation-supplied constant

The `.db` files use XTEA with 32 rounds, uint32 arithmetic, eight-byte
blocks and CBC chaining. DWORDs are little-endian. The first block supplies
the initial chaining value; each decrypted block is XORed with the previous
ciphertext block. The resulting text may end with NULs, which only the text
parser removes. No PKCS7 padding or authentication mechanism is assumed.

The public format constant is four DWORDs. It is not a personal activation
key. The module receives `DatabaseAssetKey` explicitly; its value appears
neither in the sources, nor in the fixtures, nor in the logs. The decoder
searches for nothing in the executable and modifies no activation or
protection mechanism.

An exact memory search found a single match of these four DWORDs in the
identified executable: section `.data`, file offset and RVA
**`0x00b99f38`**, VA **`0x00f99f38`** for base `0x00400000`. No big-endian
DWORD match was found. Rea corroborates four instruction references:
`0x00969686`, `0x0096976d`, `0x009abeea`, `0x009ca0ad`. Private evidence
`<private-evidence>/reprise-2026-10-30/rea/original-database/06-xrefs.json`,
Rea evidence id `ev_208807b0ce6022c66cc6999b94164559b658173209b45d211e6d0249b545abe0`.
These references do not constitute a reconstruction of those four routines.
An installation provider may read the four words after verifying the
identity of the player's copy, without asking the user for a value.

## Table text

`TDU_CarPhysicsData.db`, physical member 0 of `DB.bnk`, is 107,288 stored
bytes. After removing the initial block and decoding, 107,280 bytes remain,
including three trailing NULs. The text begins with comments for the version
(`1,2`), categories (`20`) and fields (`103`). It then contains:

```text
{TDU_CarPhysicsData} 1975083164
{REF} x
{Car_Brand} r 1209165514
...
// items: 211
```

Each descriptor supplies the name in braces, a type symbol and optionally a
reference token. Each data line contains the cells separated by `;`, usually
with a trailing separator. The official line endings include `CR CR LF` in
particular. The reader keeps cells, names, symbols and references in their
source order.

The observed symbols include `x`, `r`, `u`, `h`, `p`, `f`, `i`, `b`. They
remain exposed as raw characters. Numeric conversions are explicit; `f` in
the text does not by itself prove the floating-point type of the native
variable the game later uses. Numeric values use both the decimal point and
the decimal comma. No implicit scale is applied.

The reader requires the declared row count and the schema width. It accepts
a present or absent trailing separator, empty cells and zero declared rows.
Duplicate columns, present in the NPC data in particular, are neither
renamed nor merged; `unique_column()` reports their ambiguity. The observed
trailing NULs reach a full eight-byte block for `TDU_Brands.db` and
`TDU_CarShops.db`.

## French resource and vehicle identity

`TDU_CarPhysicsData.fr`, physical member 0 of `DB_FR.bnk`, contains 47,834
unencrypted bytes. Its `ff fe` BOM indicates UTF-16LE. Its grammar is
`{libellé} identifiant` (a label in braces followed by an identifier), with
category comments. Identifiers remain exact strings, including any leading
zeros.

`DatabaseStrings` checks UTF-16 surrogate pairs and produces UTF-8 text. It
keeps all entries, including duplicate identifiers; the unique lookup
returns an ambiguity error. Multi-line values are supported. The French
physics resource has 1,200 entries. It explicitly resolves the 211
`Car_File_Name` tokens of the tested table; no bank name is inferred from
an arbitrary row index.

## Typed view usable for future driving

`read_stored_car_drivetrain(table, row)` selects the exact names and type
symbols. It exposes REF and the car/drivetrain/gearbox tokens, the gear
count, the final drive, the seven ratios, the engine speeds and the
associated engine values. All numbers stay at stored scale. Inactive zero
ratios are kept. The units of the inertia fields remain unknown, hence the
names `engine_inertia_raw` and `gearbox_inertia_raw`.

Example attested by resource resolution: `Alfa_GT`, physical row 4, REF
`63518960`, `Car_File_Name` token `6076567`. The French gearbox and
drivetrain labels are "Mécanique" and "Traction"; they are not converted
into a native behavior enum.

| Original column | Symbol | Stored value in this row |
| --- | --- | --- |
| `Nb_Gears` | `i` | `6` |
| `Final_Drive_Ratio` | `f` | `3,7` |
| `Gear_ratio_1..7` | `f` | `3,5 ; 2,25 ; 1,52 ; 1,16 ; 0,97 ; 0,82 ; 0` |
| `Red_Line` | `i` | `7000` |
| `Power_bhp` / `Power_RPM` | `i` | `240` / `6200` |
| `Torque_Nm` / `Torque_RPM` | `i` | `300` / `4800` |
| `Weight_Kg` | `i` | `1410` |
| `Engine_Inertia` / `Gearbox_Inertia` | `i` | `50` / `14` |

The `Nm`, `Kg`, `bhp` and `RPM` suffixes are those of the original columns.
The TDUMT tool also treats `Red_Line` as an engine speed; the reader keeps
its raw name. These data do not yet demonstrate the internal conversions of
the physics engine, the full torque curve or the automatic gear change
algorithm. Those require their own routines and evidence, in particular to
reproduce the intended adaptive gearbox.

## Contracts and validations

`decode_database_resource()` returns the plaintext bytes without removing
the NULs. `DatabaseTable::parse_asset()` decodes and then owns all columns
and cells; `parse_plaintext()` allows standalone fixtures. Table read views
remain bound to the owner. `integer_value()` and `decimal_value()` refuse
partial conversions, overflows and non-finite numbers. The decimal separator
is never changed in the source cell.

Default limits are 16 MiB of resource/text, 4,096 columns, 100,000 rows,
one million cells and one MiB per logical line. Errors carry a code, an
offset/context and a message. Decryption offsets target the stored
resource; syntax offsets target the parsed text, after any UTF-16
conversion; indexed accesses use the index concerned.

MSVC x64 compilation succeeds with `/std:c++latest /W4 /WX /permissive-
/utf-8`. The **88 standalone checks** pass: two independent XTEA vectors
from [Bouncy Castle](https://github.com/bcgit/bc-java/blob/b2446db9cce193ce942894899eacc76db8b37b14/core/src/test/java/org/bouncycastle/crypto/test/XTEATest.java),
multi-block CBC, little-endian bytes, truncated blocks, schema/width/counts,
duplicates, numbers, budgets, Unicode, value ownership and the projection
of driving fields. The only keys in these fixtures are synthetic.

The official trial passes **5,407 checks**: the **18 tables**, **4,875 rows**,
**79,082 cells**, the **1,200 French entries** and the **211 driving
projections** are read without any substitute value. The constant is
supplied to the test by an ephemeral process environment from the executable
whose SHA was rechecked; that environment is restored afterwards and no
constant value is printed.

```powershell
.\build\database-local\database_test.exe
# For the official trial, the local launcher supplies the constant in memory:
.\build\database-local\database_test.exe '<installation>\Euro\Bnk\DataBase\DB.bnk' '<installation>\Euro\Bnk\DataBase\DB_FR.bnk'
```

Local log: `build/database-local/official.log`. The client's automatic
provider belongs to the installation adapter, distinct from this decoder.
No game launch or save change was performed for these verifications.
