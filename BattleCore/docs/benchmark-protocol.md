# Benchmark protocol

Run the Release build from `BattleCore/`:

```powershell
./out/build/release/battle_cli.exe bench --units 1000,5000,10000,20000 --ticks 1200 --warmup 200 --workers 1 --out out/bench/current
```

Defaults: seed 42, three independent worlds per size/mode, 200 warmup steps and 1200 measured steps, 50 ms game time per step. Timing uses `steady_clock` around `Step` only, including command validation, decisions, movement, collision, combat and copying the returned events. World creation, initial command construction, hashing and file I/O are outside timing. The initial path request runs during warmup; this is a steady-state benchmark, not a pathfinding burst benchmark. `ticks.csv` retains every measured sample; `summary.jsonl` stores nearest-rank p50/p95/p99 for each run, not percentiles of already averaged measurements.

Modes use up to 100 individual soldiers per regiment, with all soldiers moving and fighting under the same simulation rules as small scenarios:
- march: one side, explicit long MoveTo, 10 columns;
- reform: march with 5/10 column changes every 100 ticks;
- bottleneck: traffic through parallel 16 m wall openings, grid navigation and regiment collision; congestion is retained;
- volley: paired opposing regiments, 35 m bows, hit rolls and pending arrows;
- melee: paired opposing fronts at contact, 1.5 m weapons, windup and cooldown.

Combat fixtures use 100 HP to keep combat active throughout the measurement. They are sustained-load fixtures, not balance demonstrations. Separate combat/batch tests cover mass deaths and simultaneous elimination. The benchmark does not add decorative entities or replace individual attacks with aggregate damage.

`neighbor_checks` counts radius results inspected for targeting, including friendly candidates, rather than every hash-cell probe or distance test. `events` counts all returned events during the measured interval. Windows working set is sampled after each world; `process_peak_bytes` is the cumulative process peak (not a per-world allocation peak). Neither includes GPU memory. On platforms without this probe, zero means unavailable. CPU name is recorded separately in the checked-in results report.

Workers=1 is the serial reference. Requests for 2/4 currently use the same serial implementation (`workers_effective=1`), deliberately: no parallel speedup has been established. Tests compare those modes and reversed input/commit orders. Do not interpret the worker parameter as proof of multithreading.

Desired CPU budget for 5k/10k is p95 <=10 ms. Headless CPU measurements cannot establish rendering FPS, animation cost, Mass performance, Pawn cost, or packaged UE performance. Before deciding capacity, repeat in the actual UE host with its compiler, thread scheduling and camera workloads. This machine is shared with desktop/background applications; sample variation and machine contention remain possible.
