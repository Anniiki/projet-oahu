# Projet Oʻahu

[Français](README.fr.md)

An independent reconstruction of **Test Drive Unlimited PC MC 1.66 A** in
modern C++ for Windows x64, based on the original executable's behavior.
OpenTDU is a secondary reference; its engine is not imported or linked.

**Work in progress: the complete client and co-op are not playable yet.**
This repository contains reconstructed code, tests and documented findings.
Players must provide their own local game installation. No commercial game
assets, saves or product keys are distributed.

## Goals

- Reconstruct the whole game and restore every multiplayer feature.
- Render directly with **native D3D12**, without D3D9 translation, and offer
  Vulkan as an alternative backend.
- Decouple physics and AI from rendering framerate, improve automatic
  transmission behavior, and measure frame times during driving and streaming.
- Reproduce full Xbox 360 controller coverage on PC through SDL3, including
  Xbox/PlayStation identification, prompts, map navigation and hot plugging.
- Support windowed, borderless and exclusive fullscreen modes, background
  operation, variable refresh rate and display-aware frame limits.
- Ship a Windows installer for compatible two-player co-op testing, then
  reconstruct the historical online services with a modern Rust server.

The [roadmap](docs/roadmap.md) summarizes the goals and next milestones, and
the [documentation index](docs/README.md) points to each subsystem.

## Verified progress

Bank, texture, model, scene and material readers access local game data in
read-only mode. Native D3D12 viewers display original resources and explicitly
selected assemblies, with pixel, depth and resize tests. World streaming,
complete materials, gameplay and their transitions still need integration.

Fixed-step simulation, driving, configuration, input and UI modules are
reconstructed and tested separately. These component results do not
establish a complete, playable game.

The observation mod's D3D9On12 bridge is used to study the legacy executable.
It is separate from the [final native renderer](docs/native_renderer_plan.md).

## Repository layout

| Path | Contents |
| --- | --- |
| `include/tdur/`, `src/` | Assets, game, physics, input and UI |
| `src/render/d3d12/` | Native D3D12 backend and viewers |
| `tests/` | C++ scenarios and synthetic fixtures |
| `mod/` | Win32 instrumentation and isolated observation launcher |
| `docs/evidence/` | Established behavior, provenance and limitations |
| `tools/` | Release packaging; commercial resources are excluded |

## Build and test

Requirements: CMake 3.25+, Visual Studio Build Tools 2022 with C++ and the
Windows SDK, and Python 3 for test scenarios. Initial configuration
downloads the pinned official SDL3 release and verifies its SHA-256.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
.\build\Release\tdur_inspect_game.exe --game-dir "D:\Games\Test Drive Unlimited"
```

C++23 is the minimum; our MSVC targets use `/std:c++latest /W4 /WX`.
Set `TDUR_BUILD_SDL_GAMEPADS=OFF` to omit the SDL gamepad transport, or
`TDUR_BUILD_D3D12_DEVICE_TESTS=OFF` to disable standalone graphics tests.
See [dependencies](docs/dependencies.md) for versions and notices.
Scenarios requiring local game data need explicit paths and are not provided
by a plain repository clone. Legacy instrumentation needs a separate Win32 build.
[Building and testing](docs/building.md) describes the CMake presets, targeted
builds and the separate configuration of the Win32 mod.

## Downloads and roadmap

Reviewed development tools are published with their limitations in
[downloads](docs/downloads.md). The [roadmap](docs/roadmap.md) lists the next
milestones. If the project interests you, a GitHub star helps its visibility.

## Contributing and evidence

Read [CONTRIBUTING.md](CONTRIBUTING.md) and the
[subsystem workflow](docs/development_batches.md). Each reconstruction records
its source executable, addresses, bounds and unresolved dependencies.
Matching bytes establish presence, not execution or a finished game feature.
Protection, activation and reference game data remain unchanged.

Technical evidence notes are published in English as their translations are
validated; source identifiers, addresses and validation commands are preserved.
The public history is an audited selection of the local project. Commercial
captures are excluded.

## License

Project code is released under the [MIT License](LICENSE), copyright
Anniiki and the contributors to Projet Oʻahu. Third-party code retains its
own notices, including [Microsoft Detours](mod/src/vendor/detours/LICENSE.md).
Commercial game data is not part of this repository.
