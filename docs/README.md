# Documentation

The repository's [main README](../README.md) and
[contributor guide](../CONTRIBUTING.md) are available in English and French.
Research notes are published in English, and every evidence note is in the
[evidence folder](evidence/). Original addresses, type names, commands and
evidence limitations are retained in those notes.

| Area | Starting points |
| --- | --- |
| Scope and priorities | [Roadmap](roadmap.md) |
| Building and releases | [Building and testing](building.md), [development releases](releases.md), [tools package and installer](evidence/installer_tooling.md) |
| Startup and configuration | [Lifecycle composition](evidence/startup_lifecycle_binding.md), [service registration](evidence/startup_completion.md), [service cycles](evidence/startup_service_cycles.md), [mode work queue](evidence/startup_mode_queue.md), [file sources](evidence/startup_file_sources.md), [configuration loading](evidence/configuration_drivers.md) |
| World loading | [Area and sector preparation](evidence/startup_service_preparations.md), [area resources](evidence/startup_area_resource.md), [sector resources](evidence/startup_sector_resource.md), [area and sector loading](evidence/startup_world_loaders.md), [loading queue](evidence/startup_world_sync.md) |
| Native rendering | [Renderer plan](native_renderer_plan.md), [D3D12 draw batches](evidence/d3d12_draw_batches.md), [material inspection](evidence/native_material_inspection.md), [texture viewer](evidence/d3d12_texture_viewer.md), [mesh viewer](evidence/d3d12_mesh_viewer.md) |
| Game data | [Banks](evidence/bank_archive.md), [3DD scenes](evidence/scene_3dd.md), [3DG geometry](evidence/geometry_3dg.md), [materials](evidence/material_2dm_reader.md), [databases](evidence/database.md) |
| Simulation and driving | [Fixed-step calendar](evidence/fixed_step.md), [physics worker](evidence/fixed_physics_worker.md), [automatic transmission](evidence/adaptive_automatic.md) |
| Controllers | [SDL3 transport](evidence/sdl_gamepads.md), [bindings](evidence/controller_bindings.md), [mod gamepad bridge](input_gamepad_proxy.md) |
| Interface | [VMF execution](evidence/vmf_execution.md), [menu registry](evidence/vmf_menu_registry.md), [global commands](evidence/vmf_fscommand_global.md), [GetURL host bridge](evidence/vmf_host_url.md) |
| Observations | [Observation mod](mod-observation.md), [Present timing diagnostics](frametime_recording.md) |
| Dependencies | [Versions and notices](dependencies.md) |

Evidence distinguishes the official PC executable, secondary references and
captured runtime windows. A byte match does not establish routine execution,
transitive behavior or a playable subsystem. Component tests and verified game
sessions remain separate milestones.
