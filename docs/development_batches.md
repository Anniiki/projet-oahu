# Development by subsystem

## Organization

Reconstruction proceeds through functional paths: resources and scenes,
simulation and driving, interface and input, then sessions and network services.
A batch covers several dozen related routines, usually 20 to 40, within a single
Rea session. This is an indicative size: retain the actual boundaries rather
than inventing bodies or effects to meet a quota.

Each batch combines official behavior, owned C++ interfaces, client integration
and a validation scenario. An analysis report alone does not complete a
functional path. Keep the states "compiled", "tested" and "validated in-game"
distinct. Assign one writer per file; every integration includes an
independent review.

## Shared corroboration against the official executable

A shared mechanical pass inventories every reconstructed range: virtual
address, length, body or prefix, PC/Xbox subject, hash and evidence reference.
Group exact duplicates; preserve gaps and disjoint intervals explicitly.
Never invent a missing size from the neighboring function. Routines without
reliable bounds remain on the list of missing evidence.

Static comparison reads the identified official PE without modifying it and
checks section boundaries and physical file bounds. Differing windows remain
subject to corroboration during an isolated official observation, using an
explicit list of game-logic code. Equal bytes establish presence at that
location, not execution, transitive data or completion of a subsystem.
Tool failures must not trigger a cascade of dependent requests.

## Integration priorities

1. Load and assemble a scene and vehicle from local data, render in native
   D3D12, and establish the original rules that remain unknown.
2. Connect input, camera, fixed-step simulation and driving; verify vehicle
   behavior, transitions and frame times in the reconstructed client.
3. Connect proven profile states and network services, then validate
   multiplayer features and an actual co-op session on two computers.

Coordination tooling receives maintenance; expanding it is no longer a
parallel priority. The [final requirements](final_project_goal.md) retain
every requested feature, including controllers, Vulkan, the installer and
the Rust server phase.
