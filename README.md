# Projet Oʻahu

[Français](README.fr.md)

Projet Oʻahu is an independent modernization project for the original Test Drive
Unlimited on PC. Players will need their own legitimate copy of the game.

This repository provides user documentation, the roadmap and reviewed downloads.

## Current status

Development is at the component and integration stage. There is no complete
reconstructed game, validated two-PC co-op build or end-user mod installer yet.
The original-copy diagnostic reaches menus, but an offline new-game path hangs
and is still being investigated.

The available development tools inspect local installations and resources and
view textures and meshes with native D3D12. They are not a playable game or an
online client. The preserved tool build passed 107 CPU component tests on each
of Windows x64 and Win32; hosted CI excluded GPU-dependent tests.

See [downloads and limitations](docs/downloads.md) and the [roadmap](docs/roadmap.md).

## Long-term goals

- Rebuild the entire game in modern C++ as a native 64-bit PC client, using the
  official PC MC 1.66 A executable as its behavioral reference.
- Provide native D3D12 and selectable Vulkan rendering.
- Restore the original multiplayer feature set, add co-op testing on two PCs
  and provide an installer for legitimate game owners.
- Reproduce complete Xbox 360 controller coverage with current SDL3, hot-plugging,
  Xbox/PlayStation recognition and appropriate prompts, including menus and map.
- Separate fixed-step simulation from rendering FPS, improve the automatic
  gearbox, correct bugs and improve frame pacing.
- Support windowed, borderless and exclusive fullscreen, background simulation,
  refresh-aware limits and variable-refresh displays.
- Later provide a modern Rust server reproducing the original online behavior.

These are development goals. No release date is promised. Original binaries,
game assets, saves and activation keys are not distributed; original protection
and activation remain untouched.

If the project interests you, a GitHub star helps its visibility.
