# Repository Guidelines

[Français](CONTRIBUTING.fr.md)

## Project structure

Public interfaces live in `include/tdur/`; `src/` separates assets, networking,
game logic, physics, input and UI. Native rendering is in `src/render/d3d12/`.
`tests/` contains C++ validations, `mod/` contains Win32 instrumentation, and
`docs/evidence/` records established behavior and limitations. Commercial assets
and private captures are excluded.

## Build and development

Use CMake 3.25+, Visual Studio Build Tools 2022 and the Windows SDK:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

Initial configuration downloads SDL3 with checksum verification. See
`docs/dependencies.md` for pinned versions and options. Legacy instrumentation
requires a separate Win32 configuration; the final client targets x64.

## Style and naming

Use supported modern C++: C++23 minimum and `/std:c++latest` under MSVC.
Follow `.clang-format`: four spaces and no tabs. Name files
`snake_case.cpp` and `snake_case.hpp`. Separate logic, transport, rendering and
data. Make ownership, borrowing and object lifetimes explicit.

## Validation

Name tests `*_test.cpp` and register them with CTest. Cover observed branches,
errors and edge cases through discriminating scenarios. There is no global
coverage threshold. Record the configuration, command and outcome. Distinguish
compilation, component tests and in-game validation. Commercial trials use
isolated saves and read-only reference data.

## Commits and pull requests

Keep commits focused. Subjects use the French `Domaine : changement` pattern;
English contributions are welcome. Explain the problem, resulting behavior,
evidence and checks. Link an issue when applicable and add screenshots for
visual changes.

Coordinate subsystem batches and reserve files before writing. Every
reconstruction must identify its origin and unresolved boundaries. Keep unknown
services explicit. Do not publish commercial executables, saves, keys, Ghidra
projects or raw exports, or alter protection and activation. Contributions to
project code follow the MIT License; preserve third-party notices.
