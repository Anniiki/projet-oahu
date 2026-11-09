# Roadmap

[Français](roadmap.fr.md) | [Home](../README.md)

The first priority is reliable entry into a real offline game session, followed
by complete controller navigation and integration of the reconstructed systems.
Component tests and menu visibility alone do not establish a playable game.

The final client targets modern C++, x64, native D3D12 with Vulkan selectable,
fixed-step simulation, adaptive automatic shifting, display improvements and
full Xbox/PlayStation controller coverage through current SDL3.

Visual and driving modernization follows once the reconstructed client is
playable. The native D3D12 renderer remains the reconstruction step; the final
target for the graphics engine is Unreal Engine 5. This stage also targets
modern vehicle physics, smoother roads, and a modernized sky and lighting. These
goals have no date and do not change the priorities above.

Online restoration targets the complete original multiplayer experience. Co-op
testing on two separate PCs and an installer are distinct validation milestones.
A later Rust server will aim to reproduce the original online behavior with a
modern architecture.

Public releases contain only reviewed client-facing deliverables and user
documentation. Commercial game content is excluded. No completion percentage,
playable release date or validated multiplayer availability is promised.
