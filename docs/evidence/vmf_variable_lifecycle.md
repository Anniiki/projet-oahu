# Script variables and instance lifecycle

## Sources and scope

The primary reference remains the official PC MC 1.66 A executable, SHA256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The contracts below come from the Rea exports of the secondary Paradise binary
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`.
The dictionary helpers were already corroborated in the official 1479-byte
batch. This integration adds no new official observation of the wrappers or
of an in-game invocation.

| Routine | Fact used | Private export |
| --- | --- | --- |
| `445BA0` | Type 2 branch, then type 1, then text; value before name | `reprise-2026-10-30/rea/vmf-action-interpreter/session/59-procedure_assembly.json` |
| `449240/449290/4492E0` | Classification, then setter, then OR `200h` on the target | `reprise-2026-10-31/rea/vmf-text-services/session/18,20,22-procedure_assembly.json` |
| `443700/443770/4437F0` | NULL name or NULL insertion: return without assignment | `reprise-2026-10-31/rea/vmf-host-context/session-v2/29,30,31-procedure_assembly.json` |
| `448910` | If `200h` is set: scope cleanup, then node cleanup | `reprise-2026-10-31/rea/vmf-display-lifecycle/session/45-procedure_assembly.json` |
| `4438D0` | Ordered scan/compaction, scope 0 selects the non-zero entries | `reprise-2026-10-31/rea/vmf-display-lifecycle/session/57-procedure_assembly.json` |

These paths are relative to `<private-evidence>`. The envelopes keep their
EvidenceID/SHA256; two earlier reviews separate the literal catalogue from
the erroneous interpretations of a previous batch. The dictionary is at
`instance+58`, the scope at `instance+E0`, the flags at `instance+6C`.

## Owned backend and writes

`VmfDisplayRuntime::create_with_variables` owns the dictionary shared by its
instances. Its memory limit is distinct from the display's; the fixed cleanup
plan counts against the display budget. `variable_dictionary()` gives const
access, valid until the runtime is destroyed. Writes go through the display
setters, which record the need for cleanup. Earlier creations remain without
this service.

`execute_vmf_actions_with_variables` uses this backend for Get and
SetVariable: **29 opcodes, 7 host boundaries remaining**. The other entries
keep their boundaries. Without a backend, Get/Set stop before the pop.
Set pops the value and then the name, without resetting the current target.
The path is looked up by value; a miss changes neither the target nor the
flags. Texts are resolved after both pops: a numeric name can modify the
shared scratch of the bound value. Type 1 is copied bit for bit, with no
float return that could introduce an x87 conversion on Win32.

An empty key is present. An absent name (path without a colon) inserts
nothing but arms `200h` on the resolved target. A full table also produces this
native return without assignment, followed by the flag; `table_full` is
therefore distinct from the modern quotas. A quota or an invalid state returns
its nested error before the flag. The two operands already popped remain
consumed. Converting a sprite value to text keeps the explicit modern refusal
of the stack; no unestablished native conversion is invented.

The frame driver enables this path only if
`VmfFramePlayerLimits::variables` is set. The stack remains shared; budget
stops keep the cursor and do not replay Set.

## Cleanup and modern refusals

A cached instance keeps its scope and its variables. Its replacement or its
destruction schedules the parent/children/cache cleanup in the native order.
Rewind does not clean its owner: only its children that are actually
destroyed are cleaned. Scope 0 cleans the entries with a non-zero scope and keeps its own
entries. The body `448910` does not clear `200h`; a new construction resets
the flags and assigns a new scope.

`erase_scopes` applies the successive cleanups on the second, already
allocated table, then publishes once. It does not merge scopes. A late refusal
keeps the whole active table. The display prepares the calls and checks the
transaction before any change to its topology. This grouping is a modern
guard without callbacks: no intermediate state is exposed.
Publishing transform/override keeps an already armed `200h`; this adapter must
not lose track of the scope when the other flags change.

Resources 37 and the focus services keep their explicit refusals. This
integration provides neither their rendering, nor the opcode 83
connection commands, nor the time/input services of `441DF0`. No playable commercial or
co-op menu is validated.

## Validation

The `vmf_get_variable_test` binary keeps its read entry and adds `--write`
and `--lifecycle`. The tests include a real run of the frame driver on
synthetic PC 2.8/Xbox 2.5 data, exact sNaN/±0 bits, aliases, resumptions,
quotas, the cache and the late rollback. Allocation failures of the new
creation are injected one at a time. Receipts and the first fixture failures
remain in the private evidence.

The batch passes in Release x64 and Win32: **814 write checks**, **324
lifecycle checks** and the **1,034 read checks** kept, fixtures included, per
architecture. The **11 targeted CTest suites** pass in each build. This
includes the eight regression suites and the three entries of the variable
test binary. No complete commercial invocation is obtained.

```powershell
cmake --build build --config Release --target vmf_get_variable_test
ctest --test-dir build -C Release -R '^vmf_(get_variable|set_variable|variable_lifecycle)$' --output-on-failure
```

The file diagnostic also accepts
`tdur_inspect_vmf_playback <film.vmf> pc|xbox --variables` and reports in its
JSON whether the backend is enabled and how many variables it holds. It
executes no commercial binary and renders no interface.

The 39 private VMF copies were rechecked with and without the backend in both
architectures, 156 diagnostics in all: the same 37 text/focus refusals and
2 stops on opcode 83, no complete invocation. The copies keep their hashes. These
files therefore do not yet reach the execution of the new variables before
their missing boundary; the synthetic tests remain the proof that Get/Set
execute in the driver.
