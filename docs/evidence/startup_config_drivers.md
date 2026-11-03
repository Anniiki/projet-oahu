# Effects, radio and leaderboards configuration at startup

The three complete callers `98E090`, `98DBB0` and `98DA20` now compose the
existing configuration registry, section driver, argument lookup and parser
teardown. `StartupConfigurationBinding` supplies them to the derived
[startup completion](startup_completion.md). It receives the same owned
service that represents argument `1140360`.

## Evidence

Reference: official PC MC 1.66 A, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The retained primary Rea listings are
`controller-startup-host-v1/gate-session/06..08-procedure_assembly.json`.
They contain 463, 112 and 117 instructions respectively. Every listed
instruction is reachable from its entry; all internal branch destinations
are listed, and each caller ends in `RET 4`. This control-flow check does
not establish the bodies or return behavior of their callees.

The 25 referenced path formats, filenames and section labels were read
through bounded, read-only windows of the unchanged official PE. Their
section, file offset and read digest are retained in
`startup-config-stages-v1/labels.json`. The primary section-call sites and
labels form the independent test fixture
`tests/fixtures/startup_config_stages.txt`.

The function at `9EC378` has not been identified as a particular CRT
routine. Its format-shaped calls remain explicit bounded ports; the
verified strings do not turn an unknown callee into an implemented one.

## Source selection and outputs

Each file first selects prefix identity `1140444` or `11404C4` from the
current byte `[1140370]+E0`, then calls `9EC378`. Radio and leaderboards
subsequently invoke the real `run_args_lookup_986b60`, with keys `radio`
and `leaderboards` and bounds 255 and 511. Its native AL is ignored;
a modern access refusal stops the caller before loading.

Byte `F68BF0`, read after preparation, chooses the first source. A nonzero
value tries `9AFF20` first and falls back to `9A5A70` if the returned pointer
is null. Zero tries `9A5A70` first, then `9AFF20` if its raw AL is zero.
Neither caller invokes `6B1A00` or `9A5D20`, so those existing loader
implementations are not substituted here.

`9AFF20` receives receiver identity `FA4068`, the path and a live size
output. `9A5A70` receives `10E80C0`, the live block output, path, size
output, three zero stack arguments, and EAX set to zero at the call.
The outputs are never cleared by these drivers. External AL=true can use
an earlier output if the callee leaves it untouched; it is distinct from
an absent integrated resource. A present zero-length block still parses.

After an integrated-first call, its returned pointer is stored in the
block slot. For an external-first fallback, that store exists only in
the first four files of `98E090`. Its final FX file, radio and leaderboards
keep the fallback pointer in a local register without overwriting the
output slot. The parser uses that captured pointer, even when the slot
retains a different value written by the failed external attempt.

Only a successful external-source route reaches `674E0C` after parsing.
Integrated data is not freed by these callers. The captured pointer and
ownership branch remain stable across section callbacks; the unknown
free implementation is still a port.

## Files, sizes and state

`98E090` uses one parser for five files: HDRIConf, Weather, Weather_HDRI,
WeatherDesc and FX. The first four parse one section each. FX parses nine
sections with one captured size: CAR, OCEAN, GLOW-INGAME, PAUSE, GRASS, HDRI,
HDRICONF, AVATARLIGHTFACTOR and REFLECTION. Its path buffer is not cleared
at entry. After file 1 returns, service field `+29B` is set to zero; after
file 2, it is set to one. These stores also occur when resources are absent,
but not after a preceding modern refusal.

`98DBB0` clears its 256-byte path before constructing the parser, prepares
the radio filename, applies an argument override, then parses Radio.
`98DA20` similarly clears all 512 path bytes and parses Leaderboards, then
Nb_Challenges. It reloads the size output for the second section, while
retaining the same captured block. A callback can therefore change the
second size without changing the block being parsed.

Each driver constructs one real `9896C0` registry, uses that parser for all
its section calls and reaches the real `988FC0` body on a native-return
path. The shared `SectionParserState` is reused. Every modern refusal
preserves preceding writes and stops later section/free/teardown calls.
The caller frames are owned, explicit input state; their default C++
values are not evidence about uninitialized native stack bytes.

## Validation and boundaries

Seven focused x64 suites pass: 10,276 checks, zero failures. This includes
1,192 checks for these callers and a composition case that starts with
13 owned service slots, invokes the real derived completion and all three
new drivers, then reaches 51 registered members. It observes 1,125 actual
registry callbacks (three passes over the 375 registrations of the public
registry subset), 16 section-driver calls and three teardown calls.
The preexisting full base-startup composition remains covered separately.
Unknown service/callee bodies in these scenarios are explicit witnesses.

A retained initial version incorrectly stored the radio and leaderboards
fallback results into the output slots. The primary-based witness was
compiled against it and failed two assertions (1,173 checks, two failures).
Removing those writes makes the same cases pass. An initial compiler
warning in the test's optional-byte setup was also corrected; `/W4 /WX`
remains enabled rather than suppressing the warning.

The selector read, format call, integrated lookup, synchronous external
load and block release still require implementations. A nonzero external
AL with a null block is refused before an unrepresentable owned parser
call, not silently treated as an absent file. Unknown blocks, excessive
sizes and invalid parser cursors retain the existing modern refusals.

Validation used the game guard and BelowNormal priority, with at most two
compiler jobs. No game or commercial configuration data was executed or
parsed. The startup gate remains abstract at its nonordinary boundary.
These results establish component composition, not a working Oahu profile,
playable world, restored multiplayer or a two-client co-op session.
