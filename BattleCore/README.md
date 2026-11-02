# BattleCore

Portable C++20 battle simulation, independent of Unity and Unreal Engine.
Coordinates: metres, X forward, Y right, Z up; yaw in radians. Fixed simulation interval: 50 ms.

## Build on Windows

Install or locate CMake >=3.25, Ninja and a C++20 compiler. Tools are configured per process; no global PATH change is required.

```powershell
$env:BATTLE_CXX = 'YOUR_TOOLCHAIN/bin/clang++.exe'
$env:BATTLE_NINJA = 'YOUR_TOOLS/ninja.exe'
& 'YOUR_CMAKE/bin/cmake.exe' --preset headless-debug
& 'YOUR_CMAKE/bin/cmake.exe' --build --preset headless-debug
& 'YOUR_CMAKE/bin/ctest.exe' --preset headless-debug
```

Use `headless-release` instead of `headless-debug` for benchmarks.
On this development machine tools currently live under `S:/Virtual Life/VirtualLife/out/tools/`: CMake in `cmake-3.31.6-windows-x86_64/bin`, compiler in `llvm-mingw-20241217-ucrt-x86_64/bin`, Ninja in `ninja/`. These are configurable external development tools, not a runtime dependency.

MinGW standalone executables link their C++ runtime statically. The core is built without RTTI/exceptions. Tests and file tools may use exceptions to diagnose malformed input. Before Unreal/Windows integration rebuild the core with the selected UE-compatible MSVC toolchain; do not link the MinGW binary into Unreal.

Implementation plan: `../docs/superpowers/plans/2026-11-02-battle-core-migration.md`.
