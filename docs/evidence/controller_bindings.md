# Control profile: driving and menus

## Source and scope

`ControllerBindings` transposes assignments from the publisher's Xbox 360
manual, PDF page 3/printed pages 2–3, whose identity and verified rendering
are recorded in the [donor inventory](xbox360_controller_inventory.md).
This is a table for the new client, not the Xbox binary's dispatcher or
proof of playability. The library builds in modern C++ for x64 and Win32
with the SDL3.4.18 headers.

Two contexts are explicitly supported: `basic_driving`, without a modal
or owning auxiliary menu, and `menu_navigation`, when that screen owns
focus. An unknown context or action is rejected; no generic rule turns
a menu button into a driving command.

Physical SDL positions make the same profile usable on Xbox or PlayStation:
for example, SOUTH serves the action while driving and confirmation in
menus. Names and glyphs come from the identified device; PS assignments
are not attributed to a commercial PlayStation version.

## Customization and lifetime

Create defaults once with `ControllerBindings::xbox360_defaults()`.
Apply `with_overrides()` to the **current user profile**, then retain the
validated result. Unspecified actions retain their sources; an empty list
explicitly disables an action. This module opens or writes no save or
preference from the commercial game.

Validation is complete before publication: at most 42 overrides per call,
contexts/actions, at most four sources per action, SDL enums, finite
nonzero scales in `[-1,1]`, duplicate actions or sources. Rejection leaves
the previous profile unchanged. Overrides are copied into owned fixed
storage; caller spans are not retained. `sources()` allocates nothing and
returns a span tied to the profile's lifetime; do not retain it after
replacing or destroying that profile. A direct call on a temporary profile
is rejected at compile time.

## Independent validation

**2 077 checks pass per architecture**, MSVC latest `/W4 /WX`, x64 and
Win32. Oracles come from the manual and public contract, frozen before
reading the implementation. They check contexts, cumulative customization,
unbinding, copied-data independence, rejection of a late invalid entry,
duplicates, bounds and nonfinite values. A thousand consumed lookup loops
make no measured call to `operator new`. The test does not link SDL or
observe a real device.

The integrated `controller_bindings_test` target and
`controller_context_bindings` CTest also pass in both parent builds.
Private evidence: `<private-evidence>/reprise-2026-10-30/controller-bindings-cpp/`.

```powershell
cmake --build build --config Release --target controller_bindings_test
ctest --test-dir build -C Release -R controller_context_bindings --output-on-failure
```

## Behaviors still open

This module evaluates neither press/release nor dead zones, curves,
repeat, double press or simultaneous stick/D-pad merging. Menu direction
sources remain separate. These policies must be established and connected
to the transport, screens and simulation.

No profile yet exists for the open map, photo mode, radio, assistance,
players, chronopack, text entry or other specialized screens. Basic “open
map” and “cycle auxiliaries” commands do not make their contents functional.
The booklet's ambiguous L/R scrolling glyphs remain unassigned. Settings
persistence, game integration and complete Xbox/PlayStation validation
remain to be implemented.
