# Ranged performance — 2026-11-02

User authorized redesigning ranged combat for optimization. Work stays in the portable C++ core; this is not an Unreal/Mass or rendering measurement.

## Results

Ryzen7 3700X (8 cores/16 threads), Windows, Clang19.1.6 LLVM-MinGW, Release, one effective worker, seed42. Three independent runs per size/mode;200 warmup+1200 measured50ms steps. Figures below take the maximum per-run percentile/max across the three runs, rather than averaging percentiles. Elapsed wall-clock Step time includes scheduling noise and returned-event copying. Own compilation was not run concurrently with timings.

| Units | Old volley p99 ms | New4-phase p95 ms | New4-phase p99 ms | New maximum step ms |
|---:|---:|---:|---:|---:|
| 1000 | 22.453 | 1.276 | 1.438 | 1.651 |
| 5000 | 130.674 | 6.205 | 6.792 | 7.196 |
| 10000 | 268.255 | 12.915 | 14.034 | 16.263 |
| 20000 | 509.676 | 25.740 | 28.360 | 30.294 |

In this series,10k volley p99 improved 19.1x (268.255->14.034ms). The10k target<50ms and desired<=20ms are met; the measured20k volley also stays below50ms. This does not establish a game FPS or capacity limit. The earlier10ms p95 aspiration is not met by the current10k ranged fixture; the new distribution trades more moderately busy steps for much smaller rare peaks.

## What changed

1. Exact nearest-enemy search visits nearby grid cells first and prunes strictly farther cells. It avoids a sorted radius-result list per attacker. Team and dense payload index are built once; combat accesses the selected unit directly. Payload indices live only inside the immutable combat read; external IDs stay stable. No eligible attacker means no index rebuild.
2. AI first/idle acquisition uses four stable UnitId/RegimentId phases, spreading releases over four50ms steps. Exact ready_tick completion remains eligible, preserving even non-divisible sustained cooldown periods. The default20-tick cooldown retains its rate. Player-controlled archers bypass the phase.
3. Every launch, flight, hit/miss, health loss and death is still individual. No aggregate regiment damage or shortened test duration is used.
4. Commit queue was retained: baseline10k commit p99 was about2.7ms against264ms in targeting; replacing it was not justified by the profile. No multithreading was added.

## Isolating the effect

The exact-query-only checkpoint, before mechanics changed, preserved all three10k baseline StateHashes and reduced p99 to44.735ms. In the final build with cohorts1 (simultaneous release), worst p99 is46.256ms. The default cohorts4 then reduces the peak further; the improvement is not solely hiding the original slow query behind timing changes.

A400-tick archer scenario was run through the preserved baseline and new executable with cohorts1: full gameplay snapshots match after excluding only the new rule/version and the differently defined diagnostic counter; event traces are byte-identical. The default cohorts4 intentionally changes first-shot timing and can change target survival, hit rolls and outcomes. Different-build/default-mechanic hashes are not promised to match.

## Individual work and phase cost

The following deterministic counters cover the measured1200 ticks (warmup excluded), from one representative repetition; all three repetitions have identical hashes and counters.

| Units | Individual launches | Hits | Misses | Deaths | Peak launches/tick | Targeting p99 ms | Commit p99 ms |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1000 | 52898 | 23969 | 28947 | 229 | 273 | 1.278 | 0.084 |
| 5000 | 265107 | 118459 | 146750 | 1128 | 1277 | 5.994 | 0.395 |
| 10000 | 530136 | 237061 | 293261 | 2264 | 2539 | 12.237 | 0.820 |
| 20000 | 1060098 | 473731 | 586747 | 4531 | 5083 | 24.091 | 1.702 |

Combat fixtures use100HP to sustain load, as before. Cohorts change outcomes, so launches/hits need not equal the old synchronous run. New neighbor_checks counts visited spatial candidates before team/radius rejection; old counters counted radius results, so their values are not directly comparable.

## Melee regression

| Units | New melee p95 ms | New melee p99 ms |
|---:|---:|---:|
| 10000 | 7.250 | 7.544 |
| 20000 | 16.589 | 17.238 |

## Reproduce

From BattleCore, build headless-release and run:

```powershell
./out/build/release/battle_cli.exe bench --units 1000,5000,10000,20000 --modes volley --ranged-cohorts 4 --ticks 1200 --warmup 200 --out out/ranged-performance/final
./out/build/release/battle_cli.exe bench --units 10000 --modes volley --ranged-cohorts 1 --ticks 1200 --warmup 200 --out out/ranged-performance/cohort-one
./out/build/release/battle_cli.exe bench --units 10000,20000 --modes melee --ticks 1200 --warmup 200 --out out/ranged-performance/melee
```

[Baseline summaries](benchmarks/ranged-baseline.jsonl), [exact-query checkpoint](benchmarks/ranged-exact-query.jsonl), [final cohorts4](benchmarks/ranged-final.jsonl), [final cohorts1](benchmarks/ranged-cohort-one.jsonl), [melee](benchmarks/ranged-melee.jsonl). Raw per-tick phases and preserved measured executables remain under out/ranged-performance. The final I/O-only replay/rule-validation corrections do not change the measured simulation/benchmark path.

## Persistence and limits

Snapshots/replay records now require version2, with explicit ranged rule and complete persisted fields. Version1 continuation is rejected; old authoring scenarios and Unity legacy input remain supported. New schema2 save/resume/continuous/replay produce identical complete snapshots at tick200, StateHash15001352556256180108 in the archer verification fixture. That value identifies this fixture/build, not a universal battle hash.

Full Debug/Release CTest passes12 groups. Differential query tests cover ties, boundaries, Z, negative/sparse/saturated coordinates, input reversal and large-radius fallback; phase tests cover1024 archers, arbitrary cooldowns, player bypass and pending arrows after panic/source death. Real UE, visual animation, rendering, packaged builds and physics remain the later engine stage.
