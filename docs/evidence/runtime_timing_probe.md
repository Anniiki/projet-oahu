# Official clocks: read-only observation

## Method

`tdur_probe_timing` is an x64 program built in the latest C++ mode, separate
from the game. It verifies the SHA-256 of the audited official file, the
canonical path of the process, its creation date and its main image. Its handle
has only `PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ`. The reader
shared with the code probe offers no write and no remote call.

The default fixed catalogue now contains 20 scalars documented in
[the clock analysis](retail_focus_timing.md) and
[the official dispatch](retail_simulation_dispatch.md): **65 bytes per
sample**. The historical measurements below used 14 scalars.
Reads are limited to the image and to readable, committed, non-guard data
pages; no arbitrary address, length or string is accepted on the
command line. No pointer is followed. The probe reads no profile, account,
user input, key, screen or save.

It collects 26 samples spaced 200 ms apart, over about five seconds.
Each series of reads carries two QPC timestamps belonging to the probe. The
successive scalars **do not form an atomic snapshot** of the game.
The foreground PID is only compared with the observed PID; no window is
activated and no focus message is sent.

The report uses `CREATE_NEW`. An unreadable scalar or a non-finite float
becomes `null`, with an explicit count of read failures. The SHA-256
identity covers the audited file; it does not claim to measure the whole live
image, some of whose helpers differ from the file on disk.

## Program validation

The self-test reads its own known scalars: one byte, one int32 and one
float32. A fixture producer advances its counter, observed through the same
reader; code and canaries stay unchanged. Refusals cover a code page requested
as data, a forbidden size, an out-of-image range, data requested as code and
an invalid PID. The infinite-float case checks that the JSON stays valid.

```powershell
cmake --build build --config Release --target tdur_probe_timing
ctest --test-dir build -C Release -R '^runtime_timing_readonly_probe$' --output-on-failure
```

Both tests passed under MSVC 19.44, SDK 10.0.26100, `/std:c++latest /W4 /WX`;
the code probe and its test are not published in this repository.
An independent review of the catalogue and the reader confirms the types and
bounds.

## First official measurement

Private trial `20261030T083003-25076`, PID 9424, official PC MC 1.66 A copy,
SHA-256 `3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
A 75-second launch, slot acquired and PID recorded before resume; the stable
D3D12 bridge is active, with no resolution override and no change of focus
or simulation. Measurement at 40 seconds, actual duration **5.287331 s**.

| Scalar | First → last sample |
|---|---|
| uint32 clock, ms | 19736 → 25023 |
| Raw total, seconds | 18.886776 → 24.1740837 |
| Scaled total, seconds | 9.99592304 → 15.2826481 |
| Current / saved factor | 1 / 1, constant |
| Physics frequency / integer mode | 90 / 1, constant |
| Integrator entry counter | 0 → 0 |
| Application flag `16990c3` | 1 across the 26 reads |
| Game PID in the foreground | None of the 26 samples |

All 364 scalar reads succeed. The internal flag stays active while the PID
is not in the foreground; **no reception of an activation loss is
demonstrated**. The menu without a world explains the limited scope of the
zero physics counter. These data validate neither NPC behavior, nor driving
out of focus, nor a manual pause, nor connected fixed steps.

The same trial reads 27 code ranges, 6,518 bytes: the integrator 81dfd0/112,
the callback 81ee50/596 and the worker 81f0b0/223 are identical to both disk
files within the compared bounds. This corroborates the analysis of the
official timer; their presence does not prove that the callback runs in this
scene.

Private evidence:
`<isolated-copies>/slot0/20261030T083003-25076/`.
`timing-scalars.json`, 14,744 bytes, SHA-256
`c22ba848d668332cbe31c5d06371847d674d882b722ed549f5b59e10a3b196c9`.
The Job stops only our game process at its planned deadline (exit 1460), then
releases the slot. The 6,697 reference files and the 17 source saves
are unchanged.

For the next justified trial, add to the wrapper:

```text
--timing-probe build/Release/tdur_probe_timing.exe --probe-after 40 --seconds 75
```

The wrapper requires the whole measurement period to stay within the trial
duration.

## Bounded graphics extension

`--timing-profile graphics` adds eight graphics scalars identified through
the callbacks, `996850` and `74dca0`: AA/HDRI from the configuration,
AA/HDRI/LOD from the in-memory profile, then AA/HDRI/LOD from the renderer.
The budget becomes 28 values and 91 bytes per sample (22/79 before the
physics extension). Only these fields are read, without
walking the player structure or its account data. Addresses and lengths
appear in the report. The `timing` profile remains the default.

## Physics extension and windowed trial

The catalogue adds the delta selector `F8A2E8` (byte), the period
`168D6B8` (float32), the three bytes `129BEEB/EC/ED` and the last callback
MS `10E7A5C` (uint32). None of these fields is written by the probe.

The closed private trial `20261030T102129-30308`, PID 32088, lasts 75 s. The
26 graphics samples read 28 values/91 bytes without failure. The period is
`0.0111111114`, the selector is 1 (measured delta), EB/EC are 0 and ED is 1.
The last callback MS and the integrator entry counter stay at zero.
These menu observations establish no physics cadence while driving.
AA/HDRI/LOD stay at 2/1/2 in the preferences and the renderer.

The log records a 2560×1440 source and viewport, a **windowed 1280×720**
output, 160/1 Hz, a configured 160 Hz cap, `exclusive=0` and
`exclusive_pending=0`. The code probe reads 43 ranges/11,916 bytes; the 14
added ranges of the dispatch, the world, its pause and the AI match both
disk files. This proves that their content is present, not that it executes.
The Job is closed, the slot released and the 6,697 files/17 source saves
are unchanged.

`timing-scalars.json`: 26,044 bytes, SHA-256
`35bafd19af80ef13cf00491065bf9cfb2cffafbff7c1353eb02fe5cefa7eb86a`.
Log: 14,593 bytes, SHA-256
`e45f249e7f4280c1a16bc622cd31aa68ea843929fe50a54af7682d179666d7db`.
The exact comparisons remain in the private evidence
(`code-helpers/comparisons.json`).
