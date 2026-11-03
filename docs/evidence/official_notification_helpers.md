# Notification helpers in the official process

## Isolated trial of 01/11/2026

The new `ui-vmf-notification-helpers` profile of `tdur_probe_code` reads
five fixed ranges representing four complete bodies, that is **255 bytes**.
It also reads one non-executable byte at FAC598: total budget **256 bytes**.
The six alignment bytes between the two ranges of the wrapper are excluded.

| Native entry | Ranges and size | Observed role |
| --- | --- | --- |
|504930|504930/42 and 504960/23, total 65|Select the hash of a name after searching for the first NUL|
|42E1E0|91 bytes|Eight single-byte accumulators with ASCII conversion to upper case|
|451AA0|63 bytes|Formatting helper with context|
|451A70|36 bytes|Formatting helper without that context|

The supplied copy remains the official MC1.66A executable, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The reads check its path, the PID and the creation date of the process,
and the bounds and permissions of the main image pages. No free address
is accepted on the command line; no pointer is followed. The reader keeps
only its read rights.

## Result and preservation

The owned trial, PID 34204, archive `20261101T011256-35280` of slot 0,
lasted **100.199 seconds**. Each loaded range matches the bytes of the
secondary Rea analyses and their complete-body fingerprints. These ranges
differ from the primary on disk; their agreement is therefore established
in its process, without inferring the cause of this difference. The
backend 504980 already had its own historical memory evidence; it is not
reread and not counted as new evidence of this trial.

The mode byte is **1 at the time of this read**. This demonstrates neither
a default value nor its stability throughout the whole game session. The
future C++ caller must supply the observed mode explicitly.

The **6,697 reference files and 17 savegames** checked remain unchanged;
the copy uses isolated savegames and the `coop-anything` slot is released
at the end of the trial. The sources and tools tracked by the snapshots
are also preserved during the run.

## Checks and limits

The probe builds in MSVC Release C++latest, with `/W4 /WX`, in x64 and
Win32. Its owned self-test checks the exact read of a one-byte scalar,
its integrity, eight access/identity refusals and two table-size refusals.
The earlier four-byte scalars keep their size; the shared reader, already
compatible with 1/4/8 bytes, remains unchanged.
The code probe and its self-test are not published in this repository.

The complete evidence and comparisons remain outside Git in
`<private-evidence>/reprise-2026-10-31/vmf-notification-helpers-official-v1/`.
The presence of the code does not prove its targeted execution. No forced
native call or UI input injection takes place. The D3D9On12 bridge of this
trial remains temporary; the final native renderer, playable menus and
co-op are not validated by this read.
