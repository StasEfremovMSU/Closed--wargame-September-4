# Headless benchmark results � 2026-11-02

AMD Ryzen 7 3700X, 8 cores / 16 logical threads; Windows; Clang 19.1.6 LLVM-MinGW; Release (-O3, no fast-math, core FP contraction disabled); seed 42; one effective worker. Three independent runs per size/mode, 200 warmup +1200 measured ticks each. Both sets contain all 60 runs. The optimized table was measured before the final review corrections; a follow-up obstacle workload is recorded below after the collision fix.

All final state hashes match baseline versus optimized and across three repetitions. The same movement and individual combat rules are used at every size. No render, GPU, animation or Unreal cost is included. See [protocol](benchmark-protocol.md).

The table uses the **maximum per-run percentile across three runs**, in milliseconds. Raw per-run counters, memory, hashes and all percentiles are in [baseline](benchmarks/baseline.jsonl) and [optimized](benchmarks/optimized.jsonl); individual tick samples remain under `out/bench/`.

| Units | Mode | Baseline p95 | Optimized p50 | Optimized p95 | Optimized p99 |
|---:|---|---:|---:|---:|---:|
| 1000 | march | 1.000 | 0.117 | 0.145 | 0.152 |
| 1000 | reform | 1.060 | 0.113 | 0.117 | 0.146 |
| 1000 | bottleneck | 1.038 | 0.122 | 0.130 | 0.159 |
| 1000 | volley | 0.369 | 0.153 | 0.306 | 20.186 |
| 1000 | melee | 0.901 | 0.473 | 0.594 | 0.612 |
| 5000 | march | 5.123 | 0.709 | 0.751 | 0.860 |
| 5000 | reform | 5.106 | 0.700 | 0.845 | 1.080 |
| 5000 | bottleneck | 4.976 | 0.754 | 0.787 | 0.881 |
| 5000 | volley | 1.567 | 0.803 | 1.597 | 111.374 |
| 5000 | melee | 5.392 | 2.587 | 3.207 | 3.285 |
| 10000 | march | 14.037 | 1.666 | 1.886 | 2.221 |
| 10000 | reform | 8.961 | 1.651 | 1.851 | 2.450 |
| 10000 | bottleneck | 11.063 | 1.752 | 1.869 | 2.168 |
| 10000 | volley | 2.913 | 1.607 | 3.219 | 225.314 |
| 10000 | melee | 13.922 | 5.948 | 8.424 | 9.807 |
| 20000 | march | 23.772 | 4.346 | 6.309 | 6.938 |
| 20000 | reform | 34.289 | 4.284 | 4.945 | 6.167 |
| 20000 | bottleneck | 22.187 | 4.621 | 5.973 | 6.901 |
| 20000 | volley | 8.691 | 3.723 | 8.778 | 463.212 |
| 20000 | melee | 21.032 | 13.317 | 16.069 | 17.582 |

The desired p95 <=10 ms budget is met for all measured 5k/10k fixtures. **Synchronized volleys remain a major latency limit:** p99 reaches 111.374 ms at 5k, 225.314 ms at 10k and 463.212 ms at 20k. These bursts exceed the 50 ms simulation interval; the passing p95 does not demonstrate smooth real-time play. Most archers share a 20-tick cooldown, so heavy targeting/launch steps occupy roughly 5% of samples and are largely hidden by p95. Optimizing nearest-enemy queries and distributing targeting work is a subsequent measured iteration. This does not establish a capacity limit for a complete game. At 20k, use the measured values above; the same 10 ms target is not guaranteed.

The measured 5k one-sided march baseline inspected 27.6 million radius candidates during 1200 steps, although there was no enemy. Optimizations skip targeting when only one live side remains and reuse a radius-query output vector during combat. The former removes those irrelevant queries; the latter avoids per-unit output allocation. No parallelism was introduced. Next profiling targets are unit targeting/ID lookup during dense combat and regiment contact checks.

Process peak working set across these runs: baseline 25.46 MiB; optimized 25.16 MiB. This is a cumulative process high-water mark, not a per-world allocation count. Timings can vary with desktop activity; parts of baseline overlapped compilation, so the comparison is exploratory rather than an isolated laboratory measurement.

The initial grid paths are computed in warmup; initial navigation bursts need a separate latency benchmark. Combat uses 100 HP to sustain fighting, while mass-death correctness is tested separately. Worker requests 2/4 use serial fallback and are checked for identical state; no parallel speedup or Mass compatibility is claimed.

[Hardware record](benchmarks/hardware.json) and [measured executable hashes](benchmarks/binary-hashes.json) identify this local measurement. Baseline executable was built before the two optimizations; measured executables are preserved in `out/bench/baseline/battle_cli-baseline.exe` and `out/bench/optimized/battle_cli-measured.exe`. Rebuild final code with `headless-release` for normal use.


## Collision correction follow-up

After review, individual Returning/slot movement gained static-obstacle sweeps with a bounding-box rejection. Repeated the affected bottleneck workload (all four sizes, three runs each, same seed/ticks/warmup); all 12 hashes match the earlier measurements. Other benchmark modes contain no obstacles. Full Debug/Release regression suites also pass.

| Units | Maximum p95 of three runs (ms) | Maximum p99 (ms) |
|---:|---:|---:|
| 1000 | 0.169 | 0.186 |
| 5000 | 0.959 | 1.112 |
| 10000 | 3.276 | 3.694 |
| 20000 | 5.684 | 6.956 |

[Follow-up raw summaries](benchmarks/review-collision.jsonl). The main table above remains the original controlled fixture comparison; this follow-up records the cost of the final collision correction.
