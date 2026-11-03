# Building and testing

The Windows builds use Visual Studio 2022, a Windows SDK, CMake 3.25 or newer,
and Python 3 for the instrumentation test harness. Use a PowerShell terminal
with `cmake`, `ctest`, Git and the Python launcher available. No administrator
session is required. The C++ targets select their existing language and warning
settings; the presets do not replace target definitions.

A successful build produces components and tools. A component test result does
not establish that the complete game starts or that controller support is
complete. Game trials require a separately prepared installation and isolated
saves.

## Choose a configuration

| Preset | Run from | Architecture | SDL source | Device tests |
| --- | --- | --- | --- | --- |
| `x64-offline` | Repository root | x64 | Disabled | Disabled |
| `x64-sdl-local` | Repository root | x64 | Existing local source tree | Disabled |
| `win32-offline` | Repository root | Win32 | Disabled | Disabled |
| `mod-win32` | `mod/` | Win32 | Experimental transport disabled | Existing mod tests unchanged |

The first three configurations write to `build/presets/<preset>/`. The separate
mod project writes to `build/presets/mod-win32/` in the repository root. Keep
the architectures in separate build directories; do not change the architecture
of an existing CMake cache.

The root and mod projects have separate `CMakePresets.json` files. Run the mod
commands from `mod/`; selecting a root preset does not configure the mod.

Build and test presets select Release and one parallel job. They also set
`MSBUILDDISABLENODEREUSE=1` for their child processes. These settings do not
limit other terminals, stop a running game, or assign a Windows process priority.
On a workstation that is also used for playing, see the advice below.

## Configure and build one target

From the repository root:

```powershell
cmake --list-presets
cmake --preset x64-offline
if ($LASTEXITCODE -ne 0) { throw 'Configuration failed' }

cmake --build --preset x64-offline --target fixed_step_test
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }

ctest --preset x64-offline -R '^fixed_step_simulation_calendar$'
if ($LASTEXITCODE -ne 0) { throw 'Test failed' }
```

Pass an explicit `--target` when building. The preset intentionally does not
replace the project's target set with a fixed subset. A build command without
`--target` requests the generator's default build, which can compile many
unrelated tools and tests. Do not use `ALL_BUILD` for a targeted validation.

The build target and CTest name are different identifiers. `ctest -N` lists
tests without running them. CTest does not build their executables. These
presets report an error if the selected test expression matches no tests.

The same component can be checked with `win32-offline` by replacing the preset
in all three commands. The Win32 configuration checks compatibility of selected
components; the reconstructed client targets x64.

## Use an existing SDL source tree

`x64-sdl-local` is available only when `TDUR_SDL3_SOURCE` is set. Supply the
extracted source release pinned in [cmake/sdl3.cmake](../cmake/sdl3.cmake), not
an installed SDK, DLL directory or unrelated checkout:

```powershell
$env:TDUR_SDL3_SOURCE = 'D:/dependencies/SDL3-3.4.18'
if (-not (Test-Path -LiteralPath "$env:TDUR_SDL3_SOURCE/CMakeLists.txt")) {
    throw 'SDL source directory is missing'
}

cmake --preset x64-sdl-local
if ($LASTEXITCODE -ne 0) { throw 'Configuration failed' }

cmake --build --preset x64-sdl-local --target controller_bindings_test
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }

ctest --preset x64-sdl-local -R '^controller_context_bindings$'
if ($LASTEXITCODE -ne 0) { throw 'Test failed' }
```

The preset sets `FETCHCONTENT_SOURCE_DIR_SDL3` and
`FETCHCONTENT_FULLY_DISCONNECTED=ON`. It does not download SDL. The local-source
override bypasses the archive download and its `URL_HASH` check: verify the
source archive before extraction and record the source used for the build.
The pinned archive in `cmake/sdl3.cmake` is SDL3 3.4.18, SHA-256
`9cd42377704398796071b8597cd7e21da254a43bad98c4199739647adc13fa6f`.
An unset variable hides this preset; a nonexistent source must be fixed locally.
Do not switch to an unpinned download as a fallback.

Disabling D3D12 device tests does not promise that every other test is free of
windows, devices or process observation. Select the component test required
for the change and inspect its prerequisites. Full interactive and gamepad
device tests are separate validations.

## Configure the Win32 mod separately

```powershell
Push-Location mod
try {
    cmake --preset mod-win32
    if ($LASTEXITCODE -ne 0) { throw 'Mod configuration failed' }

    cmake --build --preset mod-win32 --target tdur_foreground_cursor_test
    if ($LASTEXITCODE -ne 0) { throw 'Mod component build failed' }

    ctest --preset mod-win32 -R '^observation_foreground_cursor_policy$'
    if ($LASTEXITCODE -ne 0) { throw 'Mod component test failed' }
} finally {
    Pop-Location
}
```

This target tests cursor policy through injected calls. It does not start the
commercial game or validate Alt+Tab on a player session. The preset preserves
the mod project's existing tests and leaves `TDUR_MOD_GAMEPAD=OFF`. Building a
proxy DLL or enabling its experimental transport is a distinct change from
these component checks; do not copy an unreviewed DLL into a game installation.

## Shared workstations

On a workstation that is also used for playing, do not configure, build or
test while the game is running. Run these commands at Windows `BelowNormal`
priority and keep the preset's one-job limit. A build or test command must
never stop the game or unrelated compilers.

## Check formatting and record validation

Use clang-format **19.1.5** with the repository's `.clang-format`: four spaces,
no tabs, an 80-column limit. Check only the C++ lines changed by the work. For
example, if `git diff --unified=0` identifies changed lines 30 through 45 of a
file:

```powershell
clang-format --version
clang-format --style=file --dry-run --Werror --lines=30:45 src/game/example.cpp
```

Replace the example path and range with actual changed files and ranges. Pass
multiple `--lines` options when needed. Do not format another contributor's
unrelated changes. For JSON and Markdown, review the diff directly and preserve
valid JSON, working relative links and code-block indentation.

Each validation record should contain the source commit and local changes,
selected preset and target, CMake/MSVC/SDK versions, local dependency identity,
exact commands, exit codes and logs. Record tests skipped or not run. Keep
configuration/build success, component test results and actual game behavior
as separate observations.

Preset schema and command semantics are defined in the
[CMake 3.25 presets manual](https://cmake.org/cmake/help/v3.25/manual/cmake-presets.7.html).
