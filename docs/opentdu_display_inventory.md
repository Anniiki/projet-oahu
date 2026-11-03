# Inventory of OpenTDU display changes

Read on October 30, 2026: the earlier OpenTDU-based fork, branch
`recompilation`, HEAD `1edb1fa0ac23c1629c05ef66e4543193fcc03b55`.
This inventory describes the earlier work to be carried over to the official
base; no change described here is considered already ported to the official
executable. The `refs/clone/*` branches and the separate D3D12 prototype do not
change this reference.

## Window, displays and preferences

| Commits | Main files | Behavior and evidence | Remaining port |
| --- | --- | --- | --- |
| `45916b4`, `af20bf7` | `source/player_data/opentdu_settings.*`, `system/ed_sys.*`, `system/mng_options.*`, `render/gs_render.cpp`, `config/gs_config.cpp` | Borderless default on the primary desktop, three distinct modes, `DisplayMode`/`Display` per profile. `-w` and `-display primary/N` take priority without persistence; the window is created hidden, then placed before being shown. | Rewrite the Win32 policy on the original; do not reuse the OpenTDU save structs. |
| `d4cc3de`, `0134755` | same modules | Minimum 256×256, real pixels/DPI, active profile read early; a display change no longer scrolls through the resolutions unintentionally; diagnostics of invalid options deferred until the log is opened. | Handle WM_DISPLAYCHANGE/WM_DPICHANGED and monitor migration, with preferences specific to the mod. |
| Private trial notes | `<private-evidence>/notes/` | Windowed on display 2, active profile/override, absent display, invalid values; borderless 2560×1440/160 Hz and exclusive on both displays, without crash. | Redo these scenarios on fixtures, then on the official copy. The old exclusive mode takes 800×600 without a preference: this default must not be reused. |

The borderless mode really exists in the fork. A window without decoration
does not, however, prove an exclusive full screen: that requires the
corresponding presentation state. The SDL index is not a stable Windows monitor
identity; the new bridge must follow the display actually covered.

## Focus: simulation, audio and input

`c32f8e3` touches `source/system/ed_sys.cpp`, `source/tdu_instance.cpp` and
the README. `gbIgnoreFocusLoss` is true; `-nofocuspause` and `-pauseonfocusloss`
work in every configuration, the last argument winning.
Losing focus no longer disables the simulation and no longer mutes the audio.
`SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS` is set before SDL_Init.
Evidence sources: the private construction and recap notes
(`<private-evidence>/notes/`, section on focus and gamepad in the background)
and the FOCUS_GAINED/FOCUS_LOST branches of `ed_sys.cpp`.

Input remains a separate subsystem: `input/gs_device.cpp` also applies the
suppression tied to the development interface and to disabled devices. The
official port must identify its own pause condition, its audio and DirectInput
policies. Modifying a window or keeping Present running does not demonstrate
that its simulation continues; no fake focus event is justified by the old SDL
code.

## Frame rate, presentation and VRR

| Commits | Files | Behavior/test | Remaining port |
| --- | --- | --- | --- |
| `3ea88ff` | `source/gs_timer.cpp` | Fixed deadlines, reset after more than one frame of lag. Measured 60.0 FPS instead of 59.7; 142.4–143.5 for a 144 target, limited by long frames. | QPC/timer limiter per display, without modifying the simulation step. |
| `f42d925` | `gs_timer.*`, `player_data/opentdu_settings.cpp`, `system/mng_options.cpp`, `database/port_strings.*` | `native` by default, rational refresh rate of the current monitor reread every second; `native-3` optional; fixed caps and unlimited remain possible. CLI overrides do not rewrite the profile. | Keep native as the default, follow migrations and expose the margin as an explicit choice. |
| `258e6eb`, `f8ee1b1` | `gs_timer.cpp`, telemetry | Period boundary/cap wait and actual durations corrected. | Count computation, presentation and simulation separately. |
| `4eee075`, `5410300` | `gs_timer.*`, `physics/gs_physics.*` | Fork measurements: 90 Hz physics from 60 to 175 FPS; alternation of steps/frame repeats identified. | This result concerns OpenTDU, not the original. Measure the real official tick before any physics change. |
| `e11de90`, `4c06394`, `75e243a`, `1eeb9aa` | `render/presentation.*`, vehicles/camera | Snapshot interpolation, driver frame of reference corrected, then enabled by default. | Reimplementation based on the original states; no code pasted from the old engine. |

In `render/vulkan/render_device.cpp`, VSync forces FIFO; without VSync the fork
prefers MAILBOX, then IMMEDIATE, otherwise keeps FIFO. This does not prove that
FreeSync activates. The local trial guide describes the trials at native and
native−3 rates: the DDC/CI reading stayed at 160.10 Hz even at 100 FPS; the
hardware indicator remained to be checked by Anniiki. No positive
G-Sync/FreeSync finding is inventoried.

The DXGI bridge must distinguish ALLOW_TEARING support, effective mode and
physical VRR activation. VSync disabled and capping are not enough to assert
that the monitor varies its refresh rate.

## Aspect ratios, HUD and quality

`c527553`, `411cdda`, `7838c4b`, then fixes/reviews `0af7d78`,
`85df8ba` and revert `912bcf7`: `flash/ui_layout.*`, `ui_hud_layout.*`,
`flash_mouse.cpp`, Flash display, `draw_list.*`, map and HUD. Menus in a
16:9 area, HUD at the real edges, mouse interactions adapted; 24 trials at
eight sizes in `<private-evidence>/notes/`, with no crash reported. The finishing patch
recorded at the time as not integrated must not be counted as established
validation. A swapchain change does not carry these UI/projection behaviors.

The fork exposes LOD/anti-aliasing, dense reflections and `NightLights`
Low/Default/High/Ultra (`opentdu_settings.cpp`, `mng_options.cpp`). The sources
read do not show a global "everything at maximum" preset; `NightLights` falls
back to Default without a preference. Anniiki's current request therefore adds
a new policy: maximum **supported** quality, to be established for the official
options, distinct from the graphics card's MSAA/aniso capabilities.

The old trials and preferences are a list of behaviors to carry over, not proof
that the arguments, offsets or options of another executable are compatible
with the official PC MC 1.66 A executable.
