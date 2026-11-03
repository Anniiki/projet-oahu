# Native loading of SystemPC.ini and video configuration

## Usable result

The client has a native `-system <path-token>` argument. A private path that
differs from the exact name `SystemPC.ini` selects the external reader in the
analyzed secondary loader. The expected external file is plain text; the
cumulative XOR concerns the embedded INI block. Keeping the other sections of
a private copy of the original INI, then changing the video parameters, is the
established candidate for configuring the engine during its initialization.
The actual opening of this file by the official client remains to be observed.

The four dimensions must follow `SetVideo` in the `[Video]` section, because
that callback rewrites the dimensions. Anti-aliasing and the HDRI boolean have
their own callbacks. The end of initialization can still replace the windowed
mode and vertical synchronization.

## Provenance and limits of the equivalences

x86 VA addresses, base `0x00400000`:

- Official MC 1.66 A reference: SHA-256
  `3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
- Secondary image of the Platinum/Paradise patch: SHA-256
  `f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.

Rea/Ghidra 12.2 evidence is kept under
`<private-evidence>/reprise-2026-10-30/rea/paradise-video-config/`.
The secondary session resumes from the `paradise-focus-timing` snapshot; its
60 successful calls are exported to a bundle and a snapshot, then closed.
The bounded disassemblies `video-callbacks.asm.txt`, `hdri-callback.asm.txt`
and `video-mode-tail.asm.txt` complete the Rea readings where the callbacks
are not recognized as functions. They come from MSVC dumpbin 14.44.35229.0,
without executing the image.

The closed official trial `20261030T083003-25076`, PID 9424, provides
`<isolated-copies>/slot0/20261030T083003-25076/code-helpers/comparisons.json`:

| Memory ranges compared | Observed equivalence |
| --- | --- |
| `6b1a00/512`, `438450/512` | Differ from the official disk, identical to the secondary image |
| `98e6a0/512` and `98e8a0/156` | Identical to both disks |
| `985020/128`, `985130/128`, `985260/128`, `986850/128` | Identical to both disks |

The reads cover in particular the complete bodies `6b1a00` (82 bytes),
`438450` (159), `985020` (107), `985130` (102), `985260` (117) and `986850`
(30). The remainder of each read belongs to other instructions. The equivalence
does not extend to functions called outside the ranges.

`targeted-disk-comparisons.json` also keeps the static comparisons:
`986b60/86`, `9851a0/178` and the `9896c0/512` prefix are identical between
disks. The selection helper `6b18d0/217` differs: its behavior described
below is attested only in the secondary image, without an official
memory comparison of this body.

## INI selection and external format

`98e6a0` initializes a callback context through `9896c0`, copies `SystemPC.ini`
into an initially zeroed 256-byte buffer, then calls
`986b60("system", buffer, 255)`. This helper searches the arguments for a token
starting with `-` followed by the requested name, with a case-insensitive
comparison. It copies the next token. The established form is `-system path`,
with a single dash. The initial handling of quotes while building argv has not
been established by this helper; a first trial may impose a short ASCII path,
without spaces and shorter than 255 bytes.

`6b1a00` calls `6b18d0` with the chosen name. In the secondary image, the exact,
case-sensitive comparison of `SystemPC.ini`, NUL included, selects `F9C460`
and the length `0x357`. The five other embedded INI names have their own
blocks. Any other name or path yields a null pointer.

- Embedded block: `6b1a00` decodes each byte by cumulative XOR, initial state
  zero. `6b19c0` then restores the encoding of the block.
- External file: `6b1a00` calls `9a5d20`, then returns **before** this XOR
  loop. `6b19c0` then frees the external buffer.

`9a5d20` obtains the size, allocates a buffer with a margin and appends a NUL
after the bytes read. Its read path goes through the engine file system,
`9a5730`, then in particular `608720`/`607900`, which use
`ReadFile`/`CreateFileA`. `609c00` handles the engine prefixes, the separators
and paths containing `:`. The physical path finally opened must be confirmed
in the trial logs; an absolute ASCII Windows path avoids depending on the
current directory.

A file simply added under the bare name `SystemPC.ini` does not replace the
embedded selection attested in the secondary image. The private argument
must carry another name or an explicit path. Example of a native token to pass:

```text
-system <isolated-copies>\slot0\RUN\documents\SystemPC-private.ini
```

The embedded block `F9C460/0x357`, **855 bytes**, is identical between the
disks: encoded SHA-256
`cc56ee47a70cc5a418baabd4a620152d172e32778bdd08c9add182df51370891`.
Its decoding yields the SHA-256
`faa4baa57570502c37339dcbbad578d10d490ab6b11204166606d90f7549ce26`,
starts with `[System]` and contains 58 CRLF, with no isolated LF. This
comparison writes no commercial block into the repository.

## Grammar actually used

`438450` and its helpers `4381b0`, `438250`, `4383b0`, `4379b0`, `437fd0`
form the engine parser. The pseudocode incorrectly simplifies some paths by
declaring them unreachable; the assembly keeps the repeated section search and
the command calls.

- Use ANSI-compatible ASCII text, without BOM, with CRLF line endings. The
  loops test for CR and advance by two bytes; a lone LF is not an equivalent
  line ending in these bodies.
- Keep the exact case of the sections, in particular `[Video]` and `[3D]`.
  The search also recognizes `[COMMON]`, handled during section searches.
- Command names are normalized to upper case before the search. The textual
  values of the video callbacks are compared as is.
- Use the form `SetWidth = 2560`, with spaces or tabs around `=`. `=` is not a
  separator of the command name: `SetWidth=2560` becomes an unknown name. The
  callbacks read the value token after `=`.
- `#` and `//` comments are recognized. No equivalent handling of `;` is
  established in these helpers.

## Order and effects of the video callbacks

In `98e6a0`, the same buffer is presented in turn for the sections `Router`,
`Video`, `Devices`, `System`, `3D`, `DList`, `Movie`, `Sound`, `Flash`,
`Screenshot`, then freed or re-encoded. The recognized commands are registered
by `9896c0`. The offsets below are relative to the object obtained from
`callback_context+0x8c`, not global addresses to write.

| Command | Callback | Static effect |
| --- | --- | --- |
| `SetWidth`, `SetHeight` | `985230`, `985200` | Store the low 16 bits of the number at `object+38`, `+3a` if the numeric token is valid |
| `SetWinWidth`, `SetWinHeight` | `9851d0`, `9851a0` | Same rule at `object+3c`, `+3e` |
| `SetFullScreenResolution` | `985130` | `AUTO` → integer 0 at `+44`; `MANUAL` and `DESKTOP` → 1; any other string leaves the field unchanged |
| `SetAntiAlias` | `985020` | Exact string `4` → integer 2 at `+7c`; `2` → 1; any other value → 0 |
| `SetHDAntiAlias` | `985260` | Same AA write, conditioned on a byte of the context; do not infer a universal selection from it |
| `SetHDRI` | `986850` | Non-zero number → boolean 1 at `+80`; zero → 0 |
| `SetWindowed` | `985090` | String `FALSE` → byte 0 at `+41`; any other value → 1 |

The dimension callbacks perform no resolution validation in these bodies:
keep positive values representable in 16 bits and suited to the device. The
AA integer is an **internal value**; its exact connection to the D3D creation
still has to be verified. `HDRI` refers to the engine's rendering; this write
demonstrates no activation of DXGI/Windows output HDR.

`SetVideo`, callback `9853b0`, selects PAL/NTSC among other things and rewrites
`object+38/+3a` in the observed branches, with 640×480 dimensions. Placing the
four explicit dimensions after it avoids this overwrite.

At the end of `98e6a0`, at `98e8be..98e8e7`, the windowed mode `+41` is
rewritten from `143f5ac`. The synchronization bit of `+33` is also recomputed
from that mode and from `143f5b0`, with a possible write of the VBL count. The
final value of these settings therefore cannot be attributed to the INI alone.

## Preparing and verifying a trial

On a **complete private copy** of the decoded text, keep the other sections
and adapt the video part after `SetVideo`. Example trial values, not an
automatic execution:

```ini
[Video]
SetWidth = 2560
SetHeight = 1440
SetWinWidth = 2560
SetWinHeight = 1440
SetFullScreenResolution = MANUAL
SetAntiAlias = 4

[3D]
SetHDRI = 1
```

The excerpt is inserted into the existing sections; it does not replace the
complete INI by itself. Do not XOR-encode this external file. Prepare a new
file in the isolated documents of the run and pass its path through the
launcher, without modifying the reference data.

The expected validation is the actual opening of the right file before
`98e6a0` returns, then the agreement of the surface, view and presentation
dimensions, as well as of the AA/HDRI rendering. The code comparisons of
trial 10 corroborate the routines; they do not prove that an external INI was
opened, nor that the parameters will not be replaced by a later load. No new
game launch, protection change, code, CMake or index work was undertaken in this
investigation.
