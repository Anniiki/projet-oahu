# Official trial 23: physics binding and textures

## Isolated run

Private isolated copy `<isolated-copies>/slot0/20261030T174712-27428/`.
PID **33928**, creation FILETIME **134378560639742868**, consistent across the
reports. The probes correspond to local commits `13a8cd6` and `f8ffd6f`; the
seven files of the Win32 bridge production evidence are rechecked by SHA
before launch. The official executable remains locked and verified.

The Job closes the process after **120.186 seconds**, timeout code 1460.
The coop-anything slot is released; the **6 697 reference files and 17 source
savegame hashes are unchanged**. The temporary bridge uses the legacy x86
client; this trial is not a run of the final native x64/D3D12 client.

## Two concordant binding reads

The fix described in [trial 22](official_trial_22.md) accepts exactly the
seven header words already established, that is **28 bytes**. The report
distinguishes the runtime extent `012AD000` from the loader extent `01BB0000`,
base `00400000`.

Both passes request and receive **33 bytes each**, total **66/66**, status
`candidate_concordant`. Each ends at ED and the values are equal:

| Field | Observed value |
| --- | --- |
| service vtable `01293880` | `00F27068` |
| initialization entry | `0084D570` |
| cleanup entry | `00846CF0` |
| world | `231D1A80` |
| world vtable | `009ECE38` |
| simulation | `231CFAC0` |
| simulation vtable | `009EFB90` |
| step entry | `004D46E0` |
| ED | `0` |

Page bounds, types and permissions passed the checks; the code entry is only
queried, not called. The interval between the first and last timestamp is
3 160 QPC ticks at 10 MHz. This successive concordance is **neither an atomic
snapshot, nor a proof of lifetime or of absence of ABA, nor an authorization
for a native call**. No reference counter is read.

## Texture code corroborated

The exclusive `assets-textures` profile reads **12 ranges / 1 258 bytes**: ten
complete AddressSets and two selected excerpts. All are readable, equal to the
same windows of the secondary and different from the official file on disk.
The SHAs of both PEs and their physical windows match the private Rea catalog.
No range contains a bridge adaptation.

The ten bodies are `505060/121`, `505700/71`, `509590/29`, `601F90/363`,
`5F5A30/246`, `5FC380/126`, `601C20/51`, `512620/128`, `512980/45`,
`614DF0/3`. The excerpts are `60235B/69` and `512490/6`. The evidence covers
their presence; no call, active shader, live cache, actually loaded texture
or faithful material rendering is established by this comparison.
See [the bindings and samplers](retail_texture_bindings.md).

## Clocks and remaining limits

The 26 scalar series are readable for **5.286176 seconds**.
The integrator entry counter goes from **3 385 to 3 865** (+480),
ED stays 0 and the target is never in the foreground. But `timer_scale=0`,
the scaled total stays at `4.26701212`; only the unscaled total advances from
`68.7964096` to `74.0832138`. No movement, non-zero physics delta, completed
step or exact rate is inferred from this counter.

The 32 callback captures retain timer16, x87 control word `027F`, MXCSR
`1F80`, return address `74CA75F6` classified as winmm at the time of the
logger. Their own TIDs/timestamps are not captured. The classification and
DLL identity limits of trial 21 still apply.

The private manifest pins ten reports/logs. SHA-256 of
`physics-binding.json`:
`984d0397c8e04a385af4428212f6cf2998c353122ea52d540566b689ff40c74a`;
of `code-helpers/comparisons.json`:
`23f9a3683f58de7fa6935f82a0a3073f45be069480eee46c766c08ebc949adae`.
The interface was not driven. No loaded profile, authenticated connection,
two-client trial or physics decoupling while driving is validated here.
