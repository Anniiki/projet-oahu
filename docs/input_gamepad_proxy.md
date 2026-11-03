# Experimental SDL gamepad bridge for the Win32 mod

This opt-in bridge exposes one in-process DirectInput gamepad through the
mod's existing `DirectInput8Create` export. It is transport infrastructure,
not a completed Xbox 360 control scheme for the PC game. No retail gameplay
result, menu mapping, controller glyph replacement or new-profile fix is
established by its automated tests.

## Build and activation

Configure the standalone `mod` with MSVC Win32 and
`-DTDUR_MOD_GAMEPAD=ON`. The build reuses the pinned SDL 3.4.18 source definition
in `cmake/sdl3.cmake`. A declared `FETCHCONTENT_SOURCE_DIR_SDL3` override can
reuse a previously verified local source tree for an offline build.

The default is **OFF**. An enabled build still forwards native DirectInput
unless the launched process has `TDUR_GAMEPAD=sdl`. This document does not
instruct users to replace files in their game installation. The observation
launcher's existing isolation and early-import requirements still apply.

The launcher must set `TDUR_GAMEPAD=sdl` explicitly for the child process;
without it, the native input path is kept. Requesting the bridge does not
enable probes or change focus, display or session-duration policies.

The supplied DLL must have been built with `TDUR_MOD_GAMEPAD=ON`; the request
alone does not detect that build capability and does not prove a successful
bridge activation. A requested bridge still needs runtime evidence. It does
not establish the game's real control mapping, menu support, controller
glyphs or successful entry into the Oahu profile.

## Contracts

- ANSI and wide factories share one canonical COM identity and reference count.
- The factory and each synthetic device retain their own service lease.
  Acquiring a lease during shutdown waits for the next session, with a shared
  two-second deadline. Failed initialization can be retried.
- One persistent owner thread creates, pumps and closes SDL. No SDL operation
  runs in `DllMain`, and no process-wide virtual device is installed.
- The logical device exposes five axes, ten buttons and one POV. Axes and
  buttons use SDL physical positions. Combined-trigger polarity remains a
  policy awaiting native-driver comparison.
- Unplugging publishes neutral state; a new connection can reuse the logical
  device. Foreground cooperative levels remain enforced by the device.
- Buffered events are differences between sampled states. They do not promise
  lossless capture of transitions occurring between polls.

## Verification and limits

`ctest -R "^gamepad_"` exercises the enabled build. SDK-only format and COM
tests also build with the feature disabled. The SDL tests disable physical
input backends and use an owned virtual controller, without system input or
foreground-window interaction. They verify axes, buttons, POV, buffered reads,
disconnect/reconnect, service failure recovery, shutdown deadlines and shared
leases. Fault injection is compiled only into the dedicated test executable.

The factory mock also uses the SDK's actual `c_dfDIJoystick2`, including its
164 descriptors and 272-byte state, with per-axis `DIPROP_RANGE` by object ID.
It verifies independent ranges of -1000 to +1000, exact state values, unused
fields and adjacent memory sentinels. This sequence is informed by static Rea
evidence from the **secondary** executable with SHA-256
`f452afcdacc3d69858ea8188befd4764b0994c72120892d78a7cac98c77de6ed`:
`0x4354F0` sets the format, `0x4361AB` sets each axis range and `0x4365F0`
polls and reads the state. The witness uses a mock service and background,
nonexclusive cooperation without a window. It does not validate the native
exclusive foreground path or its focus behavior.

These secondary instructions have not been corroborated on official MC1.66A.
The official static analysis did not recognize the same candidate functions;
this neither establishes their absence nor identifies a gameplay defect.

The virtual chain calls the actual service and DirectInput implementation;
it does not load the proxy through the game's executable. The real game's
requested data format, action mapping, trigger polarity and UI glyphs remain
to be observed and implemented. `BuildActionMap`, `SetActionMap` and synthetic
`EnumDevicesBySemantics` are not supported. These limits must remain explicit
when reporting progress or preparing a player trial.
