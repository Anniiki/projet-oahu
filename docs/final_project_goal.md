# Final project goal

[Français](final_project_goal.fr.md)

## Reconstructed client

Reconstruct all of Test Drive Unlimited PC MC 1.66 A, including every function
and feature, in modern C++ as a **native Windows x64** executable.
The official 32-bit executable is the primary reference; OpenTDU provides
only earlier work to inventory. Every player must own a local copy to supply
the commercial data, which the client opens read-only.

Use the latest C++ standard actually supported by the toolchain.
Currently this means C++23 and MSVC `/std:c++latest`, without claiming
complete support for a future standard.

## Required functionality

- Restore the complete game: world, vehicles, driving, NPCs, menus, audio,
  controls, saves, missions and other original features.
- Restore every multiplayer feature, followed by co-op tested with two real
  players. Use Rea for evidence and coop-anything to prepare and verify
  synchronization and reconnection trials.
- Render directly with native D3D12, without D3D9On12 or runtime D3D9
  translation, and offer Vulkan as an alternative.
- Separate physics and game logic/NPCs from FPS through fixed simulation
  steps, preserving correct behavior at high display refresh rates.
- Integrate the adaptive automatic transmission and the improvements
  inventoried from the OpenTDU work, validating them in the reconstructed client.
- Offer windowed, borderless by default, and exclusive fullscreen modes;
  the highest supported graphics settings; continued operation without focus;
  a frame limit adapted to the active monitor; and verified VRR support.

## Controllers and Xbox 360 usability

Integrate the latest stable SDL release in the C++ client. The official version
verified on 30/10/2026 is [SDL 3.4.18](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.18).
Pin the version and checksum for reproducible builds, then update them after
verifying newer stable releases.

First use must reproduce the layout and assignments established for the
Xbox 360 version while preserving custom settings. Support hot plugging,
unplugging, remapping and reconnection without restarting; Xbox/PlayStation
identification; appropriate glyphs; and keyboard/mouse ↔ controller switching
based on actual activity. Devices without a known mapping must be configurable
without requiring a restart.

**Reproduce all of TDU Xbox 360's controller coverage on PC**, with either
an Xbox or PlayStation controller. This includes interface behavior, beyond
bindings: the map, movement, zoom, focus, selection, routes, confirm/back,
secondary screens, interactions, activities, online features and required
text entry. Final validation must cover every function with a controller,
without depending on a keyboard or mouse. Include the donor version's control
options, calibration and vibration where established; input handling alone
does not complete these features.

Analyze the Xbox 360 version completely as a donor: code, resources,
interfaces and features, then compare it with the PC MC1.66A reference.
Rewrite the selected differences in modern C++. Maintain a matrix of
function/screen/action/360 evidence/PC status/validation, keeping all unknowns
and analysis-support limitations open until resolved. No commercial protection
mechanism, key or original resource may be modified or distributed.

## Smoothness and absence of stuttering

Target a final experience without client-induced stuttering, with consistent
frame times at supported refresh rates. Avoid shader compilation, blocking
I/O, unnecessary GPU waits and unpredictable allocations on the rendering
path. Prepare programs and load resources asynchronously, with verified
memory budgets and lifecycles.

Measure CPU/GPU frame times, percentiles, maxima and long frames during
driving, streaming, transitions, monitor changes, hotplug and co-op sessions.
Average FPS, an isolated viewer or a synthetic benchmark does not prove
this requirement, which remains mandatory in the final game.

## Moving from 32 to 64 bits

The final client must not depend on executing the old x86 engine.
Reconstruct pointers, sizes, alignments, calling conventions, dependencies
and thread contexts for x64. The 32-bit offsets and identifiers in files or
the protocol retain their established width; they do not automatically become
64-bit pointers.

Probes and mods injected into the official executable remain Win32 to
observe that reference. Building them does not establish the game's port.
Likewise, isolated x64 modules, viewers and their tests are insufficient to
declare the complete client or functional co-op.

## Distribution and first co-op trial

The final deliverable must be a **Windows `.exe` installer** that deploys
all project work. Every player supplies an original installation; Anniiki's
friend already owns the game on their PC. Setup must detect a
compatible base, install our client and components, then offer a simple
launch with appropriate configuration. Preserve local commercial data,
saves and custom settings.

Prepare an installable preview as soon as a real two-player driving session
can be tested. Notify Anniiki at that milestone, supplying the
same installer for both PCs, the available features, host/join instructions
and known limitations. First verify two in-game clients, their mutually
visible and synchronized vehicles, session transitions and recovery after
disconnection. Standalone tools and simulated exchanges remain component
validations.

Every package carries a client and protocol version, a component manifest
and their checksums. Check compatibility between players and provide updates,
backup/rollback and clean uninstallation. The installer must work on a PC
without development tools and supply the required prerequisites. It contains
our redistributable components; commercial files come from each player's
local copy.

Make each preview's scope explicit. The final setup must deploy the complete
modern C++ x64 client, its D3D12/Vulkan backends, controller support and every
feature required by this document.

## Next phase: complete Rust server

After completing the client and restoring 100% of multiplayer functionality,
reconstruct the TDU 1 server from the ground up in **Rust**. It must become
the reference online service used by the project's clients and reproduce
the observable behavior of the historical servers. Reference services
needed to validate the client may precede this final server rewrite.

Cover every established service: online profiles, sessions, discovery,
friends, clubs, invitations, races, results, rankings, trading and other
original features. Document and establish behaviors that remain unknown;
a simulated response does not constitute a restored feature.

Modernize the internal architecture while preserving game semantics:
efficient network processing, consistent synchronization and progression,
reliable persistence, fault isolation, recovery and observable operation.
Evaluate Anniiki's requested “Rolls Royce” standard through
latency budgets, load tests, prolonged stability, resilience to loss/duplication
and disconnection/reconnection scenarios. Define target thresholds and
capacities before server validation.
