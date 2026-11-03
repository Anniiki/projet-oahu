# Native configuration of the official renderer

## C++ path and preserved data

`tdur_prepare_video_config` verifies the audited official executable, then reads
only the 855 bytes of its embedded SystemPC.ini, at raw offset
`00b9c460` (VA `00f9c460`). The decoding and the external path are established by
[the loader analysis](retail_video_config_loader.md). No commercial file
is written, and the preparer refuses an output under the source installation.
Decoded configurations stay in the private folders, outside Git.

The program preserves the other sections and the unknown commands. It
replaces the affected video keys and places them after `SetVideo`, which rewrites
the dimensions. The result is ASCII without BOM, with CRLF and spaces around
`=`. The four dimensions are bounded to the engine's uint16 fields.

Without explicit dimensions, the pixels of the primary desktop come from
`EnumDisplaySettingsW(ENUM_CURRENT_SETTINGS)`. The preparer queries the
system D3D9On12 factory: filterable/renderable FP16 compatible with D24S8 to
request HDRI, then MSAA 4× or 2× supported on the affected color/depth
formats. These are declared capabilities, not proof that the engine
created all the corresponding targets. HDRI does not mean DXGI HDR output.

The wrapper copies this work into the run's `documents/SystemPC-private.ini` and
passes `-system` through the launcher. The real path remains ASCII without spaces,
shorter than 255 characters; the file is in clear text on this external path.
The launcher refuses a file outside the private documents and modifies
no activation argument. Each trial keeps the hashes of the tools,
of the produced file and of the requested selection in `preflight.json`.

```text
--d3d12-display --display-mode borderless
--video-config-preparer build/Release/tdur_prepare_video_config.exe
```

This option concerns the official test bench. The launcher of the recompiled game
and its preferences menu remain to be built.

## Executed validation

- C++latest build with MSVC 19.44, `/W4 /WX /permissive- /utf-8`.
- Transformation on a witness: dimensions after `SetVideo`, old
  occurrences removed, comments and other sections unchanged; refusal
  of dimensions outside uint16, unknown AA and section syntax without CRLF.
- The real launcher passes a private INI with spaces to the CRT witness, which reads
  its exact content. This validates the launcher's quoting, without demonstrating
  how the game's parser handles it; the commercial run avoids them.
- After integration: **26/26 CTest** of the main base and **15/15** of the mod
  pass. No commercial game is launched by these CTest runs.

## Official trial 11: corrected internal resolution

Trial `20261030T085042-18800`, PID 33228, 90 seconds, same official
MC 1.66 A copy. Preparer selection: 2560×1440, AA 4× and HDRI requested.
The private file is 912 bytes, SHA-256
`471a819144e8074aa94277b793ac8b90aa4a2ba1774263fc88231e0ac3710281`.

The first successful presentation records:

```text
on12=1 mode=1 source=2560x1440 format=22 msaa=0 quality=0
viewport=2560x1440 output=2560x1440 borderless=1
refresh=160/1 cap_millihz=160000 tearing=1 focus=0
```

The surface **and** the viewport now use the native pixels. The
bridge no longer artificially forces the backbuffer size. The format field
is a `D3DFORMAT`: 22 means X8R8G8B8, not the value 22 of DXGI_FORMAT.
MSAA zero describes this single surface; the engine also has an
intermediate MSAA target. These metadata do not allow concluding that AA
or HDRI are really active throughout the chain. The maximal preset remains
to be verified with the consumed values and the intermediate targets.

DLL: `4bb92cf71611c03f9e124c183cb59d91638789ac76953b59a454ecd617f9ab82`.
Launcher: `cb16e8acb65b49e174604b251ff01c85db76048050422ccac7c944e00cc1bb5f`.
Private log, 14 142 bytes, SHA-256
`a3980c0bf7f0aa61a049826ab57872f1530b72cb4787857f017c024c4d2c4a40`.
The startup report has the hash
`af4717106c42d93d3aed3cb38a59c988cc7f5808145362b61f8fe151eeb5919b`.

The probe reads 27/27 code ranges and 26 scalar series without error.
The Job closes only our game at the timeout, slot released; the 6 697 files
and 17 source saves remain unchanged. No capture of the game's pixels,
profile manipulation or physical VRR activation is validated here.

## Trial 12: settings actually consumed

Trial `20261030T090308-28852`, PID 30820, 65 seconds, adds the eight
graphics scalars of the probe. The 26 series read their 79 bytes without
failure and find the same values throughout the measurement:

| Setting | Native configuration | Graphics preferences in memory | Renderer |
|---|---:|---:|---:|
| AA, internal enum | 2 | 0 | 0 |
| HDRI | 1 | 0 | 0 |
| LOD | n/a | 2 | 2 |

The INI options are therefore indeed read, but the graphics settings
consumed afterwards remain AA/HDR disabled; the LOD is already at level 2.
This finding is more precise than the backbuffer MSAA alone. A maximal
policy must intervene at the right preference initialization stage, without
writing the renderer's derived fields or touching account loading.
No such write takes place in this trial.

The initialization body `996850..996b7e` is read in two ranges, 512 and
303 bytes. The 815 bytes are identical on both disks; they corroborate
in particular the copy of the AA/HDR preferences to the renderer. The total
code budget is 29 ranges, 7 333 bytes. The complementary static SDK dump is
kept in `rea/original-video-settings/gs_render_initialize_sdk.asm.txt`.

Scalar report: 20 791 bytes, SHA-256
`832df9cf873c04affd1dd083393b920787dc218da8223733ab308a415dba7aac`.
Log: 13 216 bytes, SHA-256
`36142ab9c15cd33fd01bbeb8ccb806489ee5fb3782774ce73cc91e3e8037cf98`.
The trial is closed, slot released, and the same 6 697 files/17 source
saves are unchanged. The preference values are isolated graphics
scalars; no other profile field is read.

## Trial 13: maximal policy at startup

`retail_video_defaults.cpp` applies the wrapper's explicit choice after a
successful public `CreateDevice`, before the initial path `996850` rereads the
preferences. The SHA-256 of the official file and the RW data pages of the
image are verified. Only the nine AA/HDRI/LOD bytes of the preferences are
written; no derived renderer field, account, game code or protection.
The `renderer+24` flag bounds the graphics bootstrap; it does not detect a profile.

The policy does not run on `Reset` and closes at the first DXGI Present
`S_OK`. If a write fails, the HRESULT remains a failure, the old values
are restored if possible and initialization is refused before the device is
associated with the presenter. An incomplete restoration allows no silent
continuation. The fixture triggers a real callback refusal, checks the null
device output, the empty registry, then a clean recovery. The injected fixture
enables the policy on its own EXE: the identity is refused, without a write.

Trial `20261030T092715-30268`, PID 14764, 75 seconds. Two initial creations
apply the values, then the policy is closed on the first frame.
The 26 series of the probe then verify:

| Setting | Configuration | Preferences in memory | Renderer |
|---|---:|---:|---:|
| AA, internal enum | 2 | 2 | 2 |
| HDRI | 1 | 1 | 1 |
| LOD | n/a | 2 | 2 |

The effective LOD level was already 2 and stays so; writing it into the
preferences alone would not be proof of a renderer change. The first
presentation still succeeds at native 2560×1440 borderless, 160/1 Hz output.
The AA/HDR settings are therefore now consumed by the renderer. The
descriptions of the intermediate targets and the visual aspect remain to be verified;
no Windows output HDR or physical VRR is claimed.

DLL: `2dd09a3a33731c5aa34d33620008fcc21e2c6890ee638d0a05d9b0b221406bc5`.
Log, 14 636 bytes, SHA-256
`1b07f790fd2df488dbd795a38cdc33be7645428a8c59013f27c118e5f371bc32`.
Scalar report, 20 794 bytes:
`7c8bab78c2e79b8771129bcd3826cc36166dab298b122d84e4be4b0e5e997604`.
Trial closed and sources unchanged: 6 697 files, 17 saves.

With the preparer, this startup maximum is now the default of the
bench; `--keep-graphics-defaults` keeps the native comparison of trials
11/12. After the policy closes, the engine's preferences and fallbacks
are not reimposed by this mod.
