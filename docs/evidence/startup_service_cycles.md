# Startup service cycles and refusal propagation

`startup_service_cycles.hpp` supplies the four mode-controller callbacks for
`Service982c40` and `Service9b3f80`. Initialization reuses the existing startup
module. Forward, reverse and shutdown execute the reconstructed bodies below.
The host can bind the reconstructed area/sector preparations through
[PreparedServiceCyclePorts](startup_service_preparations.md). Their loading
and drain operations, and allocation release, still require host bodies.

## Primary evidence

Reference: official PC MC 1.66 A executable, SHA-256
`3e371d4918ff2b05ce9cb291162fcefee6378305227cefff2d367c4e132a9a30`.
The existing Ghidra atlas was consulted first. Bounded disassembly of the same
unchanged PE established instruction order, register arguments lost in the
decompiled signatures, and a short return omitted from the function catalog.

| Body | Defined bytes | Behavior |
| --- | ---: | --- |
| `9B4120` | 69 | Capture the global object, call two preparations, reset a member, set a final byte |
| `9B3F30` | 1 | Return without effects |
| `9B3F40` | 51 | Free and clear `+04`, then `+08` |
| `982C00` | 51 | The same cleanup logic at a second entry |
| `6FC0F0` | 70 | Guarded initialization of selected bytes and DWORDs |

Static tables `F03648` and `F03534` contain initialization entries `9B3F80`
and `982C40`, shared forward/reverse entries `9B4120`/`9B3F30`, and shutdown
entries `9B3F40`/`982C00`. Their file-backed object pointers at `FA4068` and
`FA3F90` match the earlier capture. The constructor at `9B3F90` writes
`F03648` into its receiver. These static associations do not establish runtime
installation. The host explicitly supplies its owned service instances.

The local evidence record retains ten byte-window hashes, both table contents,
six atlas-file hashes, bounded disassemblies and equal before/after image hashes.
No commercial bytes or decompiler output are distributed with this module.

## Captured identity and exact writes

`9B4120` captures `[FA48B0]` once. Its receiver is not the current service.
`708D50` receives the captured object's `+08` through EAX, `+76FD0` through ESI,
and stack arguments for the object and `+7A750`. `7053E0` receives `+9C0`
through EDI and stack arguments for the object, `+08` and `+7A750`. Their atlas
entries corroborate those register inputs. `CycleMemberRef` preserves object
identity and member offsets without creating native pointers. Ports must keep
the captured object alive; replacing the global slot does not replace it.

The actual `6FC0F0` body operates on its `+20` member. If byte `+02` is zero,
it clears bytes `+01` and `+00`, then nine DWORDs at `+04`, `+32C`, `+330`,
`+334`, `+65C`, `+660`, `+664`, `+98C` and `+990`. It writes byte `+02 = 1`
last. Any nonzero guard preserves every byte. The caller then sets its own
`+8A3B1` byte, even when the helper guard prevented initialization. Neighboring
bytes are retained. Owned default values do not establish native initial state.

The reverse callback implements the verified single `RET` at `9B3F30`.
It is not an empty substitute for an unknown body.

## Cleanup and refusal propagation

Cleanup reads `+04`, frees it when nonnull, then clears the slot after the call.
Only then does it read `+08`, free its current value and clear it. A callback
replacement of `+04` is therefore cleared, while a replacement of `+08` is
passed to the second free. A nonnull zero-capacity allocation still reaches
the free port. Clearing a modern allocation also clears its capacity metadata;
this does not imply an additional native size store.

The three cycle methods return `ModeCallOutcome`, distinguishing a returned
native call from a modern refusal inside the reconstructed body. Native cycle
callbacks return void; no AL meaning is inferred. On refusal, the controller
does not visit another service, start the later forward pass, drain another
shutdown member or write the terminal `UINT32_MAX` count. Earlier effects,
including the native pre-shutdown decrement and callback mutations, remain.

`StartupCycleService` stays abstract until initialization is supplied. The two
adapters reuse `initialize_982c40` and `initialize_9b3f80`, owned fields and the
existing mode registry. Neither preparation port has a successful default;
the separate preparation binding now supplies their reconstructed drivers.

## Validation and remaining work

The new suite composes both adapters with the actual registry and actual
`9B40C0`/`9B4060` helpers, then exercises forward, reset and reverse-order
shutdown. Required preparations and memory ownership are explicit test ports.
Cases check every reset byte, four guard values, captured identity, callback
mutations, both cleanup refusal sites, repeated cleanup, nonnull zero-capacity
storage, and all three controller refusal paths.

With typed bodies connected but the previous controller still ignoring their
outcomes, two focused suites produced nine failed assertions. The three
controller checks fix those same cases. The validation record retains both
runs, the final suite counts and commands.

Build `startup_service_cycles_test` and run CTest with
`-R '^startup_service_cycles$'`, under the game guard, BelowNormal priority and
one compiler job. This suite establishes cycle behavior. The preparation
bodies have their own linked suite; a full host installation, a loaded world
and playable multiplayer remain unproven.

Related: [startup initialization](startup_services.md),
[mode lifecycle](game_mode_lifecycle.md), and
[startup composition](startup_lifecycle_binding.md).
