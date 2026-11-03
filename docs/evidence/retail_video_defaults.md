# Initial video values, preferences and intermediate targets

## Result

The AA/HDRI values of the configuration file and the preferences consumed by
the renderer are distinct. The initialization `996850` creates the device,
then copies the local AA/HDR preferences to the renderer at `996b0a..996b24`.
These preferences are zero-initialized by `8980c0`. An AA4/HDRI1 request in
the INI is therefore not enough to establish the effective renderer values.

The renderer also creates a target named `RT_MAIN_MSAA`. The format and MSAA
of the presented backbuffer do not by themselves describe this intermediate
target. The preferences, the renderer parameters and the resources actually
created must be observed separately.

For a C++ reconstruction, separate the default values, the loaded preferences
and the state derived from the resources. For the private mod, the explicit
policy under consideration, "maximum quality at startup", is a launch choice,
not a detection of a fresh profile. The public hook after `CreateDevice`
precedes the initial copy of the preferences; this handling must not be
extended to `Reset` reinitializations, where the parameters have already been
captured.

## Identity and evidence

x86 VA, preferred base `0x00400000`:

- Official PC MC 1.66 A executable: SHA-256
  `3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
- Secondary Platinum/Paradise: SHA-256
  `f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.

The Rea/Ghidra 12.2 investigation of the secondary uses the
`paradise-video-config` snapshot. Local outputs are under
`<private-evidence>/reprise-2026-10-30/rea/paradise-video-defaults/`:

| Files | Question established |
| --- | --- |
| `02..09` | Clear, local initialization, video reapplication, device selection |
| `10..24` | References to the scalars, callers, mode change and quality |
| `25..46` | Local constructor/reset, presentation parameters, device creation |
| `47..63` | Copy to the renderer, intermediate targets, format tables |
| `65..78` | Instructions and bytes of unrecognized entries, CreateDevice/Reset distinction |
| `79..80` | Resource release after Reset |

Function requests on `9de1b5` and `994840` are refused because these addresses
are not recognized as functions. The refusals are kept; their
instructions/bytes and static MSVC dumpbin 14.44.35229.0 complements give the
bounded ranges. `996850` is likewise handled through instructions and bounded
assembly, without inventing a Ghidra signature for it. The bundle and the
snapshot are exported and the session is closed. No game was launched by this
investigation; no account content or login code was read.

`targeted-disk-comparisons.json` contains the SHAs and the equalities of the
complete static ranges: `8980c0/465`, `898db0/306`, `74dca0/508`,
`898b80/292`, `898a20/220`, `994490/890`, `993b50/174`, as well as
`996850/815`, are identical between the disks. The secondary body `50a030/391`
and the parameter constructor `509cf0/266` differ from the official disk:
their analysis does not constitute an unmeasured official memory equality.

The closed official trial `20261030T090308-28852`, PID 30820, compares in
memory `996850/512` then `996a50/303`, that is the complete body of 815 bytes.
Both ranges are identical to both disks. Respective SHAs:
`30669f7bbbf3f0bf284fa5fd37d9f98269bcac1f85003dfddbd70dc28c625453`
and `70010eb1fdc6aa7b6c53b6471158194680bccfbf28d77c004221d4b1f8fd12be`.
Report: `<isolated-copies>/slot0/20261030T090308-28852/code-helpers/comparisons.json`.

## Three sets of values

The offsets of the INI callback are established in
[retail_video_config_loader.md](retail_video_config_loader.md). The call
`9de1b0` pushes the address `114c0c0` then calls the constructor `898b80`.
Its `LocalPlayerData` starts at `object+a4`, hence `114c164` for this instance.

| VA | Type/length | Observed role | Trial 12 |
| --- | --- | --- | --- |
| `011403dc` | 32-bit integer / 4 | AA request of the configuration service (`1140360+7c`) | 2, request corresponding to AA4 |
| `011403e0` | boolean / 1 | HDRI request (`1140360+80`) | 1 |
| `01161e7c` | 32-bit integer / 4 | AA preference (`LocalPlayerData+15d18`) | 0 |
| `01161e80` | boolean / 1 | HDR preference (`+15d1c`) | 0 |
| `01161e84` | 32-bit integer / 4 | Quality/LOD preference (`+15d20`) | 2 |
| `0114b0ac` | 32-bit integer / 4 | Renderer AA parameter (`1140a00+a6ac`) | 0 |
| `01149b2c` | 32-bit integer / 4 | Renderer HDR parameter (`+912c`) | 0 |
| `0114b0b0` | 32-bit integer / 4 | Renderer quality/LOD (`+a6b0`) | 2 |

The 26 samples of trial 12, with no unreadable read, keep these values during
their window of about five seconds. The complete catalog counts 22 scalars and
**79 bytes** per sample. This agrees with the initial copy of the preferences,
without constituting a trace of every write or a proof of the render resources
finally allocated.

The absence of exact Ghidra references on `11403dc/e0` does not indicate an
absence of use: the callbacks access the object through offsets. Likewise, the
clear uses an ESI base, not the global preference addresses as literals.

## Origin of the preferences and limit of profile detection

In `8980c0`:

- `8981e5` writes zero on four bytes at `ESI+15d18`: AA;
- `8981eb` writes zero on one byte at `ESI+15d1c`: HDR;
- `8981f7` copies the active quality `114b0b0` to `ESI+15d20`.

Two direct callers are observed. The constructor `898b80` calls the clear at
`898c54` on its embedded object `+a4`. The reset `898a20` calls it at `898a36`
when its BL flag is true. This reset itself has several entries, including the
wrapper `898cb0` and a scene path. Globally modifying the clear would
therefore affect more than the first construction.

`898db0` is an initialization guarded by `object+a0`. It allocates a data set,
sets this guard to one, then continues with initializations and service calls.
In particular it resets the default dimensions and the quality from global
values. The guard is set **before** these continuations: it means that an
initialization/allocation has started, not that a preference is absent or
loaded. For the global instance it is located at `114c160`.

Neither `AA==0`, nor `HDR==0`, nor this guard allows a profile to be qualified
as fresh. Zero can be a valid preference. The loading paths of the personal
data and of the login were not followed for this question. This investigation
therefore establishes no detector of a "profile without settings".

## Initial copy and reapplication

The body `996850..996b7e`, corroborated in official memory, builds the graphics
context and calls `50a030` at `996ae1`. After its success:

1. `996b05` calls `986a90` for the ratio/dimensions handling.
2. `996b0a..996b10` copies `1161e7c` to `renderer+a6ac`.
3. `996b16..996b24` builds the HDR boolean of `renderer+912c` from the byte
   `1161e80`, then copies it again to `renderer+9130`.
4. The initialization continues with the resources through `990370`/`995520`
   and marks `renderer+24=1` at `996b71`.

The preference is therefore read **after** the device creations of this path.
A modification of the configuration fields `11403dc/e0` alone is not the
source of this AA/HDR copy.

`74dca0` is a reapplication path. After waiting for activation and checking
device parameters, it compares the desired dimensions, mode, AA and HDR with
the active values. If necessary it calls `995140` with the local preferences.
Its fallback explicitly calls with AA0/HDR0, then resets the quality to 1 and
copies the effective values back to the preferences. Choosing a fallback is
part of the original behavior; replacing it with maxima on every pass could
prevent convergence.

The menu callback `6c60c0` also copies the active values back into these
preferences when its validation argument is zero. These locations therefore
remain a mutable preference state, not storage reserved for a launch policy.

## CreateDevice and Reset are not interchangeable

In the secondary, `50a030` prepares the parameters through `509cf0`, then calls
`5061d0`/`513530` to create the device. Branches select a reference device type
or look for the `NVIDIA NVPerfHUD` adapter. The function then performs a memory
estimate through texture allocations and releases. This does not prove a direct
AA/HDR overwrite.

`509cf0` fills a `D3DPRESENT_PARAMETERS` and may reduce its MSAA to zero if the
capability check fails. Its presentation table maps the internal indices 0/1/2
to the MSAA types 0/2/4. This secondary path remains distinct from the renderer
AA values intended for the intermediate resources.

During a reapplication, `995140` first writes AA into `this+a6ac` (`99519b`)
and HDR into `this+912c` (`9951ca`). Then `509fd0` uses
`IDirect3DDevice9::Reset`, virtual slot `+40`, on the existing device. After
success it calls `994840`, a 13-byte trampoline to `994330` that releases the
affected resources. `994490` then rebuilds them.

There is no `CreateDevice` call in this normal path of `509fd0`. A callback
limited to `CreateDevice` is therefore not triggered there. Conversely,
reimposing the preferences after this `Reset` would come after their capture
by `995140` and could make the cache differ from the parameters actually
consumed. This distinction corrects the first late-rewrite hypothesis
formulated during the investigation.

The inspected bodies of `509fd0` and `994330` do not modify `renderer+24`.
This graphics byte, `1140a24`, can help identify the bootstrap phase. Not all
of its aliases and stop paths have been established, however; it is not a
profile detector and must not become a global invariant without further
validation.

## Intermediate MSAA/HDR target and quality

`994490` reads `renderer+a6ac` and `renderer+912c`. It creates a target named
`RT_MAIN_MSAA` through `50afa0`, with the renderer AA index. The internal
format passed is 2 without HDR and `0x13` when HDR equals exactly one.
The format table contains respectively 22 (`D3DFMT_X8R8G8B8`) and
113 (`D3DFMT_A16B16G16R16F`). The complete creation contract and its possible
fallbacks still have to be checked on the effective resources.

The `format=22` field of `display-first-dxgi-frame` comes from
`D3DSURFACE_DESC::Format`: it is the **D3D9** format of the backbuffer, not
the DXGI enumeration. The log code also reads only its `MultiSampleType`. It
does not observe `RT_MAIN_MSAA`.

The setter `993b50` accepts the qualities 0, 1 and 2. It uses respectively the
distance factors 0.25, 0.5 and 1, and another factor 0.6, 0.8 and 1. It
publishes the choice in `1161e84`. Trial 12 already has quality 2 in the
preference and the renderer; no lowering of this setting is observed.

## Integration policy and next validation

For the reconstructed C++, initialize a distinct `VideoDefaults` structure,
then explicitly apply the loaded preferences and the capability checks. A
choice saved as zero remains a valid choice. Do not use the reapplication paths
as permanent initializers.

The private policy proposed during this investigation is different: the user
explicitly chooses maxima at startup. It can copy the validated AA/HDR request
of the configuration and quality 2 into the **nine bytes** of the graphics
preferences, after a successful bootstrap `CreateDevice` and before the
`996b0a` read. This operation must not be presented as a guaranteed
preservation of already loaded preferences: it is an explicit initial choice,
whose values may then be kept by the game.

Bound it to the private wrapper, to the audited image and to the three known
data ranges; keep the validation of writable data pages, the real image base
and the limits. Touch neither the derived renderer fields, nor the neighboring
bytes, nor the clear/constructor code, nor the protection. Any GPU query takes
place outside `DllMain`.

Closing the policy after the first real DXGI presentation is a launch bound,
but does not prove that the maximum survived all earlier resets. A clear or a
reapplication before this presentation can still replace the preferences. The
log must distinguish initialization attempt, fallback, policy request and
effective state, without reimposing the maximum in the `Reset` callbacks.

The next useful validation will record only the authorized graphics scalars
before/after this point, then the descriptions of the created targets: format,
dimensions, MSAA type/quality and creation success. Do not infer global AA/HDR
from the backbuffer. After the policy closes, check that a setting change and
a capability fallback remain possible. No new code, CMake, index, game launch
or save change is delivered here.
