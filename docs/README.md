# Documentation

The repository's [main README](../README.md) and
[contributor guide](../CONTRIBUTING.md) are available in English and French.
Original addresses, type names, commands and evidence limitations are
retained in the research notes.

| Area | Starting points |
| --- | --- |
| Scope and priorities | [Final requirements](final_project_goal.md), [subsystem batches](development_batches.md) |
| Native rendering | [D3D12 draw batches](evidence/d3d12_draw_batches.md) |
| Game data | [3DD scenes](evidence/scene_3dd.md) |
| Simulation and driving | [physics worker](evidence/fixed_physics_worker.md) |
| Controllers | [SDL3 transport](evidence/sdl_gamepads.md), [bindings](evidence/controller_bindings.md) |
| Interface | [VMF execution](evidence/vmf_execution.md), [menu registry](evidence/vmf_menu_registry.md), [global commands](evidence/vmf_fscommand_global.md) |
| Observations | [menu helper provenance](evidence/vmf_menu_action_helpers.md) |
| Dependencies | [Versions and notices](dependencies.md) |

Evidence distinguishes the official PC executable, secondary references and
captured runtime windows. A byte match does not establish routine execution,
transitive behavior or a playable subsystem. Component tests and verified game
sessions remain separate milestones.
