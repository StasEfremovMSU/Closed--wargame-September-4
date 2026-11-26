# BattleCore

Portable C++20 battle simulation, independent of Unity and Unreal Engine.
Coordinates: metres, X forward, Y right, Z up; yaw in radians. Fixed simulation interval: 50 ms.

The [era module catalog and architecture](docs/era-modules.md) records the eight main eras, their factions and the path from scenario rules to data-driven modules.

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

## Command hierarchy

[Detailed Russian reference](docs/command-hierarchy.md) describes the operational Army, Part, Group and Regiment state machines, officer succession, tasks, reports, communications, map perception and persistence. The brains use deterministic rules; no neural evaluator or model runtime is connected.

Run `./out/build/debug/battle_command_demo.exe` (or the Release equivalent) for a small command battle. `fixtures/command-battle.json` is a runnable four-level scenario; command tasks can enter through the regular `--commands` JSON interface as `CommandKind::IssueCommandTask`.

## Run and inspect

From `BattleCore/` (PowerShell):

```powershell
./out/build/debug/battle_cli.exe import-legacy --source fixtures/legacy --map map-backup.txt --out out/legacy.json
./out/build/debug/battle_cli.exe run --scenario fixtures/duel.json --commands fixtures/orders-attack.json --seed 42 --ticks 401 --capture-every 200 --out out/artifacts/battle-view
./out/build/debug/battle_cli.exe render --snapshot out/artifacts/battle-view/frames/200.json --out out/artifacts/battle-view/200.svg
./out/build/debug/battle_cli.exe run --resume out/artifacts/battle-view/snapshot.json --ticks 100 --out out/resumed
```

Snapshots contain complete simulation and command runtime state. `--replay-out FILE` records the initial scenario, seed and supplied commands; `replay --replay FILE --ticks N --out DIR` reproduces them. See [scenario and save formats](docs/scenario-format.md).

Generated views (recreate with the commands above and navigation tests):
- Bottleneck [0](out/artifacts/navigation/0.svg), [200](out/artifacts/navigation/200.svg), [400](out/artifacts/navigation/400.svg): a column crosses the wall opening, with no teleport or wall penetration. The view follows the formation/route; long obstacle ends are clipped.
- Battle [0](out/artifacts/battle-view/0.svg), [200](out/artifacts/battle-view/200.svg), [400](out/artifacts/battle-view/400.svg): initial ranks, contact, then losses and closing ranks. Blue/red are sides, grey crosses fallen, green rings player control; labels mark panic when active. Coordinates and geometry come directly from snapshots.

SVG files open in any browser. Output is a diagnostic top view; animation, camera and physical melee belong to the Unreal stage.

## Unreal boundary

[Adapter contract](docs/unreal-adapter-contract.md) defines owned presentation frames, stable IDs, deferred updates, event deduplication and possession. The mock adapter is tested without Unreal. Actual Mass entities, animation, Pawn, rendering and a UE build remain Task 12 after the engine is installed.

[Benchmark protocol](docs/benchmark-protocol.md) explains fixtures, samples and limits. The current implementation is serial, including requests for 2/4 workers; tests verify equivalent results, without claiming a parallel speedup.

[Initial measurements](docs/benchmark-results.md) record the first delivery. [Current ranged optimization](docs/ranged-performance.md) reduces10k volley p99 from268ms to14ms in the new comparison. Individual combat remains; rendering is unmeasured.


A resumed snapshot cannot currently be exported as a new scenario-based replay; `--resume` together with `--replay-out` is rejected. Continue with snapshots or replay the original command log. Returning soldiers stop at static walls; individual detour routing is a later extension.

## Current ranged rules

AI archers use four deterministic firing/acquisition phases by default, with at most150ms initial/idle acquisition delay. Exact cooldown completion stays eligible; player-controlled shooting has no added phase delay. Set `rules.ranged_cohorts` to1 in a scenario for synchronous AI release. Snapshots/replay bundles use schema3 and include the full command state; plain schema2 files migrate automatically, while command-enabled schema2 files and schema1 continuation files are rejected. Legacy TXT/scenario authoring import remains supported. See [ranged results and reproduction](docs/ranged-performance.md).

Set `rules.local_melee` to `true` for individual movement during regiment contact. A front fighter closes to weapon range, the ranks behind compress into available space, and fighters on a free edge can continue around the formation with a stable turning direction. The wing of a wider front can turn the edge of a narrower front on passable ground. Stragglers can fight without fixing the whole formation in place. Movement respects body clearance and terrain. A melee strike keeps its target through the windup. At contact, formed archers draw weak, short-range daggers. Formed archers fire at opposing formations only while both formations are free of melee contact.

A positive `RegimentSpec::skirmisher_count` deploys up to that many archers as a screen when the regiment has a commander. Detached archers exit around the flank or rear, then advance beyond the front rank when the terrain permits. They can target other screens or a formed regiment outside melee contact. `CommandKind::SetSkirmishers` recalls or redeploys the screen. Group command positions can be roots with physical commanders outside regiment membership. Standard bearer and musician assignments can improve local morale while the relevant staff are present.
