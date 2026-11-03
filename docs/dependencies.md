# Reconstructed client dependencies

## SDL3

Official stable version verified on **30/10/2026**:
[SDL 3.4.18](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.18),
released on 02/10/2026. `cmake/sdl3.cmake` downloads the official source archive
and verifies its SHA-256:
`9cd42377704398796071b8597cd7e21da254a43bad98c4199739647adc13fa6f`.
The first configure requires network access; sources and builds remain in
`build/_deps/`, outside Git. Declare any local FetchContent override alongside
the validation results.

SDL is built statically from its own sources, including its mapping database
and usual drivers; no OpenTDU engine code is imported. Its examples, internal
tests, installation and uninstallation are not added to the project targets.
Our strict `/W4 /WX` options apply to our targets, not third-party sources.
The SDL license accompanies the downloaded sources and must be retained in
the final distribution.

The gamepad transport is enabled by `TDUR_BUILD_SDL_GAMEPADS=ON`. To build
only the other modules, configure with `-DTDUR_BUILD_SDL_GAMEPADS=OFF`.
Integrating the transport does not yet validate the game's mapping or full
Xbox 360 functional coverage.

When updating, check the latest **stable** release in the official repository,
change the URL and hash together, then validate hotplug, reconnection,
remapping and Xbox/PlayStation detection on x64. Record the exact version
in the evidence; do not track a moving branch.
