# Intermediate render targets: formats, MSAA and depth

## Result and scope

Official trial 19 directly attests a 2560×1440 FP16 MSAA4 surface, its
D24S8 MSAA4 depth and full-size and quarter-size FP16 textures. The
observation section below gives the tuples, logs and limits.

The renderer values AA2/HDR1/LOD2 do not by themselves prove its allocations.
The creation path may reduce the AA or replace the requested format; a
non-multisampled FP16 texture and an FP16 MSAA surface are also two different
objects. The backbuffer presented as X8R8G8B8/MSAA0 does not describe them.

This investigation establishes the requests of the official builder `994490`,
then the fallbacks and D3D9 calls of helpers analyzed in the secondary. **These
helpers differ from the official disk**: their equivalence in the official
process is not established. Public observation of the resources remains the
decisive verification, without pixel reads, capture, interface or personal
data. The static investigation modified no code, preference or save. The
observation wiring carried out afterwards is described separately below.

## Identity and retained evidence

x86 VA, preferred base `00400000`:

- Official MC 1.66 A, SHA-256
  `3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
- Secondary Platinum/Paradise, SHA-256
  `f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.

The existing exports `paradise-video-defaults/49..60` are consulted first.
Their snapshot is then reopened for the derived helpers that are strictly
needed, with summaries before assemblies. The Rea/Ghidra 12.2 session is
exported and closed cleanly: **377 pieces of evidence**, bundle and new
snapshot kept private under
`<private-evidence>/reprise-2026-10-30/rea/retail-render-targets/rea-session/`.
The instruction listings serve to check the ABI; `float` types or missing
arguments in the pseudocode are not treated as a signature.

The private comparisons first verify the two complete SHAs, then their PE
mappings. `disk-comparisons.json`, `helper-body-comparisons.json` and
`tables.json` retain the SHA of each range. The body bounds below run from
the start to the last RET of their exported listing; they do not constitute
a memory comparison of an official process.

| Range | Bytes | Equality of the two disks |
| --- | ---: | --- |
| `994490`: target construction | 890 | Yes; SHA `e56da849f41af6097326a256425d6d9d682f36dcefa473c87cac45441a8eb9cf` |
| `98f380`: texture preparation | 171 | Yes; SHA `d9d50d8f08eb97cf3b8081844813f36b99e9bc77c31105fa96637c91d86a8af2` |
| `50afa0`: target wrapper | 74 | No |
| `5f5980` / `5fd3e0`: allocation / creation | 127 / 682 | No |
| `509710` / `512f60`: AA conversion / depth compatibility | 23 / 88 | No |
| `50b090` / `5f58e0` / `5fd800`: views sharing the surfaces | 24 / 114 / 116 | No |
| `50aff0`: reduced-dimension target | 85 | No |
| `505880`, `505bd0`, `5058f0`, `504a30`, `506070` | 106 / 70 / 184 / 79 / 198 | No |

The tables at `f471bc` and `f472bc`, the depth portion `f474bc` and the names
at `f08078` are identical on both disks. No decoding or protection change is
undertaken on the non-equivalent official bodies.

## Requests established in 994490

Let `W = [fac8d4]`, `H = [fac8d8]`, `A = renderer+a6ac`.
The HDR path is taken only if `renderer+912c == 1`.
The quarters are integer divisions truncated toward zero; for positive
dimensions, `W/4` and `H/4`. These dimensions come from the renderer, not
from DXGI.

| Logical object named at the callsite | Observed request |
| --- | --- |
| `MainRTwM`, storage `renderer+8f80` | Full-size texture; internal format 19 if HDR, otherwise 2. |
| `ScnDown4`, `renderer+4e28` | Quarter-size texture; same format branch. |
| `MainRT`, `renderer+8fa0` | Full-size texture; format taken from the description of `MainRTwM`. |
| `RT_MAIN_MSAA`, handle `renderer+8fc0` | Full-size surface; internal format 19/2, requested AA `A`, internal flags 6/0. |
| Handles `+8fc4`, `+1d40`, `+1d44` | Three views derived from the same main handle, flags argument 8. |
| Handle `+4e48` | Another quarter-size target, format read from the parent and AA requested again; internal flag 1. |

Internal formats 2 and 19 translate through the table to **22
(`D3DFMT_X8R8G8B8`)** and **113 (`D3DFMT_A16B16G16R16F`)**. These are D3D9
values, never DXGI numbers. The second describes four 16-bit floating-point
components; its existence does not prove a Windows HDR10 output.

`98f380` prepares a generated `.2DB` representation, with two dimensions and
two arguments equal to 1. The builder receives, among others, the engine flag
`0x200`. **This internal flag is not `D3DUSAGE_DYNAMIC`**: converting it
directly into a D3D9 Usage would be an error. The exports followed end in a
virtual load call; they are not sufficient to name its native CreateTexture
implementation or to prove all of its options.

The name `RT_MAIN_MSAA` is a diagnostic argument at the callsite. `50afa0`
does not consume its value: it checks five arguments and then calls
`5f5980`. The name is not passed in the public D3D9 signature.

## Fallbacks and surfaces in the secondary

In `5fd3e0`, the internal capability byte `fadaa8[format]` is checked:
bit 0 for the base case, bit 2 for index AA1, bit 3 for AA2. If the
requested AA is unavailable but the base case is accepted, the index becomes
zero. If the base case is missing, the creation fails. The construction of
this capability byte is not reconstructed here.

`509710` then converts indices **0/1/2 into NONE/2/4 samples**; an
out-of-range index returns NONE. AA2 therefore means neither MSAA2 nor MSAA2
quality. In the creation calls analyzed, the **MSAA quality is zero**.

`512f60` calls `IDirect3D9::CheckDepthStencilMatch`, slot `+30h`, with the
monitor format, the requested RT format and the value at `f474bc`, that is
D24S8. The wrapper's second argument `0x40` is not consumed. A failure
replaces the internal format with 2 before the native call; it does not mean
a simple FP16 blending test. The AA is not recomputed in this second branch.

The native color creation at `5fd571` is:

```text
CreateRenderTarget(device, W, H, formatD3D9, aaD3D9, 0,
                   FALSE, &colorSurface, NULL)
```

Without bit 0 of the internal flags, a depth is created at `5fd5f9`:

```text
CreateDepthStencilSurface(device, W, H, depthFormatD3D9, aaD3D9,
                         0, FALSE, &depthSurface, NULL)
```

Its format comes from the **dynamic** index `fac8e4`, converted by the same
table: 64→D24S8(75), 65→D24X8(77), 66→D24X4S4(79), 67→D16(80),
68→D24FS8(83). The initial on-disk value zero of `fac8e4` is not the value
of the configured device. **The effective depth format is not established.**
Bit 5 also allows reuse of an existing depth in other cases.

For the main flags 6/0, the branch creates its own depth; for the quarter
with flag 1, it omits it. The three views through `5fd800` take over the
parent's color/depth pointers and AddRef them: they make no new native
allocation. Distinct logical objects can therefore share exactly the same
physical surface.

## Public ABI and sufficient observations

The Windows SDK 10.0.26100, `shared/d3d9.h`, is checked by nine local
assertions with no failure (`sdk-abi.json`). On x86, COM methods are
`STDMETHODCALLTYPE`/stdcall, self pointer first, four-byte stack arguments,
HRESULT in EAX. Do not forget self in a typed hook.

| Interface/method | Slot | x86 offset |
| --- | ---: | ---: |
| Device9::CreateTexture | 23 | `5c` |
| Device9::CreateRenderTarget | 28 | `70` |
| Device9::CreateDepthStencilSurface | 29 | `74` |
| Texture9::GetLevelDesc | 17 | `44` |
| Surface9::GetDesc | 12 | `30` |

The three creations have eight arguments after self. CreateTexture receives
`Width, Height, Levels, Usage, Format, Pool, ppTexture, pSharedHandle`.
CreateRenderTarget receives `Width, Height, Format, MultiSample, Quality,
Lockable, ppSurface, pSharedHandle`. CreateDepthStencilSurface replaces
Lockable with Discard; do not confuse these booleans with pointers.

The observation of the **three** creations is explicitly prepared,
textures being filtered by `Usage & D3DUSAGE_RENDERTARGET`. After success
and a non-null output, GetLevelDesc(0) or GetDesc returns a 32-byte
`D3DSURFACE_DESC`: Format, Type, Usage, Pool, MultiSampleType,
MultiSampleQuality, Width, Height. No locking or pixel access is needed.

The requests and their HRESULTs must stay separate from the effective
descriptions and from the HRESULT of their getter. GetDesc does not report
Lockable, Discard, shared handles or mipmaps other than the described level.
Native arguments and returns stay unchanged; no COM reference is kept for
telemetry. The presenter's internal creations are excluded and repeated
tuples may be counted to bound the log.

For capabilities, test the right adapter/device type, its desktop format,
the format/Usage/Type combination, then the MSAA of **both color and
depth**. The color/depth compatibility and their quality levels are also
required. The success of another format, of the backbuffer or of a D3D12
device alone does not replace them. See the Microsoft contracts for
[CreateTexture](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-createtexture),
[CreateRenderTarget](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-createrendertarget)
and [CheckDepthStencilMatch](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3d9-checkdepthstencilmatch).

## What the next log can establish

A successful RT CreateTexture followed by an FP16 level proves an allocated
FP16 texture. Its MSAA is NONE: the texture API has no MSAA parameter. This
does not refute an FP16 MSAA4 surface created separately. A successful
CreateRenderTarget followed by a GetDesc of FP16/MSAA4/quality0 proves that
surface; a CreateDepthStencilSurface and its GetDesc prove its own
description, even if its dimensions and MSAA match the color.

Without the third interception, the independent depth would be missing from
the catalog. Even with it, the surfaces implicitly created by CreateDevice or
Reset do not necessarily go through these three public methods. The absence
of an event therefore does not demonstrate the general absence of a resource.

These descriptions prove neither the name `RT_MAIN_MSAA`, nor a color/depth
binding, nor the execution of a shader, an MSAA resolve or a tone mapping,
nor the effective use of the resource in a frame. Several targets have the
same tuples, and views may share the pointers. The order of the creations or
a "probable" size is not sufficient to assign them a name. A documented call
RVA helps correlation; naming them would require additional, distinct
evidence.

## Historical baseline: trial 17

Trial 17 closed, PID 26820, folder
`<isolated-copies>/slot0/20261030T112335-28184/`.
The file `timing-scalars.json` has SHA-256
`e67802a20da347b33b8cee4bc083ac813a5eab97b1661f33f803ac76016f9148`.
Rereading it verifies 26 samples, zero unreadable scalars, preferences
**and** renderer AA2/HDR1/LOD2 stable. The reads are not an atomic snapshot.
The first logged DXGI presentation, HRESULT zero, indicates
source/viewport/output 2560×1440, D3D9 format 22, MSAA0, quality0, On12=1.

This rereading confirms the evidence problem: it attests the settings and
the backbuffer, not the intermediate targets. The existence of their official
FP16/MSAA4 allocations remained to be observed at this stage. The static
investigation launches no commercial executable and performs no interface
inspection; the following trials are run separately by the project.

## Public observation and control of the instrumentation

`--graphics-metadata` enables three public interceptions: creation of
textures with RT/depth usage, `CreateRenderTarget` and
`CreateDepthStencilSurface`. The creation parameters, objects and HRESULTs
are passed through unchanged. The getters are called only after success and
a non-null output; `desc_valid=0` indicates an unavailable description.
Formats are **D3DFORMAT** values, not DXGI indices. No image or COM
reference is kept by the observation.

The log is limited to **64 distinct tuples per device plus one notice**. The
key contains the displayed fields, the two HRESULTs and the call type;
identical requests are grouped. These are descriptions, not a count of GPU
allocations or resource names. The API counters and the getters continue
after the cap. `Levels`, `Lockable` and `Discard` are passed through
unchanged but are not attested by these tuples.

The source-owned fixture passes 102 checks: FP16 texture, MSAA4 surface,
matching depth, failure HRESULT, exclusion of ordinary managed textures and
of the presenter, grouped repetitions and diversity limit. It does not open
the game and reads no pixel. The previous version passed 98 checks but kept
only the first 64 calls, without grouping.

Trial 18 closed `20261030T120132-19748`, PID 33012, 75 s: the first record
reaches that old limit. It contains a full-size depth and 63 identical
1024×1024 targets from the memory estimation pass. The absence of FP16 in
this truncated record does not prove any graphics fallback. This measurement
justified grouping the descriptions without raising the maximum volume.

## Attested official allocations: trial 19

Trial closed `20261030T121220-31820`, PID 31120, 75 s. The 41 logged tuples
all have `desc_valid=1`, creation S_OK and description S_OK; no limit is
reached. Among them:

| Call type | Dimensions | Effective D3DFORMAT | Effective MSAA |
| --- | --- | --- | --- |
| RT texture | 2560×1440 | 113, A16B16G16R16F | None |
| RT texture | 640×360 | 113, A16B16G16R16F | None |
| RT surface | 2560×1440 | 113, A16B16G16R16F | 4×, quality0 |
| Depth | 2560×1440 | 75, D24S8 | 4×, quality0 |
| RT surface | 640×360 | 113, A16B16G16R16F | 4×, quality0 |

These resources have usage and pool consistent with the requests. They
receive no assumed name; their binding and their participation in the frame
remain outside this observation. The first presentation remains a final
X8R8G8B8/MSAA0 surface, source/viewport/output 2560×1440, borderless,
160/1 Hz, configured cap 160 Hz and tearing eligible. The 26 graphics
samples keep AA2/HDR1/LOD2 in the preferences and the renderer.

The job terminates only its own copy, slot released; the 6,697 files and
17 source saves remain unchanged. The continuity-without-activation bridge is
applied and closed within its initial scope. No loaded world, physical VRR or
multiplayer path is validated by this menu trial.

DLL: `4f1172261dd5f255c6983e0036505ace1d9c93f1382553a40440c42b67f4ed8f`.
Log, 28355 bytes:
`ce35ae005d57c566835a3e862b4c602069093b1c0de344d0cf3078cdb196108b`.
Report, 2579 bytes:
`95d686b8a7ddf164a0edfd601e276d121e107c97156682a26182c01417405043`.
Scalars, 26047 bytes:
`1cd8c54c3505847715f2f515c3922a6e69fd356f3efe6e3fb43b9ef2ce3817fb`.

The private batch also contains `graphics-resources.json`, 17,919 bytes,
SHA-256
`deb35ccaf46a72d29bf2888e72c028daf5b052d612f2106226eebdad99285b0c`.
The 41 tuples are normalized there and the six request/description groups
(dimensions, format, MSAA, quality, usage, pool) agree for each of them.

## Main Rea references

- `paradise-video-defaults/49` (`994490`):
  `ev_93388c87263688442f463e02c4c005efa534c8be5012bc0b58f3b64447f01d61`.
- New session `/12` (`98f380`):
  `ev_0cf722dbd85014ec676acbe3402342bef64762bd19c8366c70e222c57d1a6f8d`.
- `/15` (`5fd3e0`):
  `ev_79eee572369bc846c3183362ae4f4a717fabad989574ef319856f102edc9a123`.
- `/17` (`509710`):
  `ev_6b02d7232c28611e8627be06f9446b27c5e5f24f4632772758fecac7895b1543`.
- `/19` (`512f60`):
  `ev_55d53ad6325b15aa37c92c11e63f3d1afd80a8e03226babac86e9b647b68cbe7`.
- `/28` (`5fd800`):
  `ev_97830c897cd8f3366f94c71ff0a95ae6e289f0cb8fd68fc523be39efe0e53dfe`.
