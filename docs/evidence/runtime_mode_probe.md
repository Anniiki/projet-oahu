# Passive game-mode observation

This diagnostic tool helps distinguish the modes reached during an isolated
official-game test. It does not implement a world loader or fix a new-game hang.

## Reference and interpretation

The reference is PC MC 1.66 A, SHA256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
Private evidence documents global VA `0x010E7238`, the expected vptr
`0x00F0B138`, and setter `0x009B4530`. The setter checks one byte at `+0x108`;
when zero, it writes the previous mode at `+0x104` only if the new DWORD mode
differs, then writes the current mode at `+0x100`.

Identifying this object as the dispatcher's `this` remains an ABI/lifecycle
hypothesis. Sampling does not prove what PC `0x0097CE80` reads. Intermediate
transitions, including a change followed by its reversal, can be missed.

## Scope

`tdur_probe_mode` builds for Windows x64 with the latest supported C++ mode.
It verifies the executable file's hash, process path, creation FILETIME,
WOW64 identity and main-image size. It opens the target with query-limited
and memory-read rights. Its fixed catalogue exposes no address option.

Each sample reads the global pointer (4 bytes), the vptr (4 bytes), and the
two modes plus guard (9 bytes). It repeats these reads, with a maximum of
6 reads and 34 bytes. A changed pointer or vptr stops the second pass before
reading more object fields. Unreadable, executable, guarded and overflowing
regions are refused. Double-read stability is not an atomic snapshot.

The output must be new. It stores at most 64 changes, classified guard values
and counters, without object addresses or profile text. The default interval
is 50 ms, the minimum 25 ms, and duration is limited to 120 seconds. No target
writes, hooks, thread suspension, input or window-text reads are performed.

```powershell
tdur_probe_mode --pid <PID> --creation-filetime <FILETIME> `
  --exe <verified-copy.exe> --preferred-base 0x400000 `
  --out <new-report.json> --duration-s 30
```

## Validation and remaining limits

`runtime_mode_probe_core` checks exact reads, byte-sized guards, wrong types,
pointer bounds, concurrent changes, coalescing, output limits and options.
`runtime_mode_probe_win32` exercises the actual page reader on its own pages,
including refusal across an unreadable boundary and an unconsumed guard page.
These checks do not establish that a player can start a new game.

The isolated-start wrapper accepts `--mode-probe <tool.exe>` and
`--mode-probe-seconds <1..120>`. Sampling starts after `--probe-after` and must
fit within the bounded trial with one second of margin. It uses the launcher's
exact integer creation FILETIME after slot registration and runs the observer
as a separate, hidden child. The wrapper owns its cleanup and preserves the
save/reference audit if cleanup fails. Play sessions refuse this diagnostic.
`official_start_mode_observer` verifies this flow with fake processes.

The loaded executable is identified through its file and process metadata;
its loaded code is not compared with the file. A killed observer may leave
an empty report because the complete JSON is written at the end.

## Official startup baseline, 2026-11-02

A fresh isolated copy was launched for 45 seconds, using the existing display
bridge and private saves, without input or window-text collection. Sampling
began two seconds after resume and lasted 30 seconds. The observer returned
success with 600 samples: 373 null-pointer samples followed by 227 available
samples, no memory/type errors and no unstable samples. It recorded:

| Time since sampling began | Mode | Previous mode | Guard |
| --- | --- | --- | --- |
| 18,660 ms | 0 | -1 | zero |
| 22,506 ms | 18 (`0x12`) | 0 | zero |

These are observed values, without assigning an unverified meaning to mode
`0x12`. This did not exercise `ngam`, create a profile, load the world, or
reproduce the reported new-game hang. The launcher's exit 1460 was its planned
45-second deadline. All 6,697 reference files and 17 source-save files remained
unchanged; the slot was released and no game process remained. Detailed reports
are kept in the local `mode-startup-v1` evidence and isolated trial directory.
