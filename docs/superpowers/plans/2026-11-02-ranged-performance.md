# Ranged Performance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Reduce ranged tail latency at10k without losing individual combat/control/replay.
**Architecture:** Instrument Step, replace radius-result targeting with exact nearest-enemy search and dense payload indices, then distribute AI archer acquisition/release over deterministic cohorts. Keep pure read/ordered commit and serial reference.
**Tech Stack:** C++20, existing CMake/CTest/doctest/Clang19.1.6; no new dependency.
**Spec:** ../specs/2026-11-02-ranged-performance-design.md

## Global Constraints
- Work in D:/September 4, current codex/battle-core branch; preserve old dirty Unity assets, stage only explicit BattleCore/docs paths.
- C++20,50ms tick, no UE installation/dependencies, no paid API/network publishing.
- User delegates arbitrary ranged changes; native execution retained, no extra design approval pause.
- Stable IDs, player bypass, individual damage, deterministic replay; no render FPS claim or fabricated speedup.
- Step profiling is optional/ephemeral; snapshot/replay schema2 explicitly rejects old continuation files; scenario authoring schema1 remains supported.

## Review Focus
1. Nearest-query pruning must retain equal float-distance/smaller-ID candidates at cell boundaries, Z offsets and large/sparse coordinates (Task2).
2. No eligible shooter, no enemy or all dead/panicked must remain inexpensive and produce no phantom attacks (Tasks2/3).
3. Non-divisible cooldowns and player acquisition/release must not silently lose cadence or impose the AI phase on a player (Task3).
4. Save/resume between cohort phases with pending arrows must reproduce; old snapshots must fail explicitly (Task3).
5. Measured reduction must include unchanged individual work; report p99/max and compare cohort1/4 so spreading cannot conceal a slow exact targeting kernel (Task4).

## Task 1: Optional phase profiling and measured baseline
**Files:** BattleCore/include/battle/Simulation.h,src/Simulation.cpp,tools/Benchmark.{h,cpp},tools/Main.cpp,tests/BatchTests.cpp.
**Interfaces:** StepProfile{double orders_ms,movement_ms,targeting_ms,commit_ms}; Step(...,const NavigationService&,StepProfile* profile=nullptr). BenchmarkResult carries vector<StepProfile> phases and max_ms; CLI summary stage p99, raw CSV phases.
- [x] RED batch.profile_is_observational: enable/disable profiling for the same seeded30-step battle, identical StateHash; nonnegative phase times. Observe missing API failure.
- [x] GREEN implement optional steady_clock timing of four disjoint stages, with profile zeroed even on rejected input; benchmark stage percentiles/raw output, max step.
- [x] Build Debug/Release, full CTest0 failures. Save measured baseline executable and baseline volley at1k/5k/10k/20k,200warmup+1200measured,3reps. Capture hardware/compiler/seed. No concurrent compilation during timings.
- [x] Commit task; keep diagnostic files under BattleCore/out/ranged-performance and durable summaries under docs/benchmarks/ranged-*.

## Task 2: Exact nearest-enemy query with dense payload indices
**Files:** BattleCore/include/battle/Spatial.h,src/Spatial.cpp,src/Combat.cpp,tests/NavigationTests.cpp,tests/CombatTests.cpp.
**Interfaces:** UnitPosition appends int32_t team and size_t payload_index (default0); NearestEnemy{id,payload_index,distance}; optional<NearestEnemy> SpatialIndex::FindNearestEnemy(Vec3,float,int32_t,uint64_t* checks=nullptr) const. Existing radius methods unchanged.
- [x] RED navigation.nearest_enemy_matches_radius_reference: deterministic randomized/tied/Z/boundary/negative/sparse-ID/reversed/large-radius cases compare exact selected IDs+payloads; no friend/dead entry; invalid input returns none. RED navigation.nearest_enemy_prunes: thousands of distant enemies plus one near enemy examine fewer candidates than input size.
- [x] GREEN near-cell-first traversal with conservative bounds, stable tie and bounded fallback. Combat builds team+payload records once, reads chosen target by index; skip index build with no eligible attacker.
- [x] All Debug tests green; compare cohort-disabled target/event behavior with radius-reference selection. Release micro/volley10k check profile before further architectural changes.
- [x] Commit task. Decide from profile whether commit queue is material; any queue rewrite requires a separate ledgered ruling/test, not speculative replacement.

## Task 3: Deterministic AI firing cohorts and persistence
**Files:** BattleCore/include/battle/{Scenario,Combat,Snapshot}.h,src/{World,Combat,Snapshot}.cpp,tools/{JsonBindings.h,ReplayIo.cpp,Benchmark.cpp,Main.cpp},tests/{Combat,Replay,Batch}Tests.cpp,tests/TestSupport.h.
**Interfaces:** Rules.ranged_cohorts uint32_t default4, valid1..64; RangedPhase(UnitId,RegimentId,uint32_t)->uint32_t; AI eligible at exact nonzero ready_tick OR tick%cohorts==phase, provided cooldown ready. Player and melee bypass. Snapshot/replay schema2, scenario schema1.
- [x] RED combat.ranged_cohorts_distribute_and_preserve_cadence:1024 healthy AI archers, all shoot within4 ticks, each phase nonempty and bounded, repeat at20-tick cooldown; cooldown5 remains5 once firing, not rounded to8. cohort1 matches simultaneous behavior.
- [x] RED combat.player_ignores_ai_phase_and_control_switch: manual shot before AI phase allowed; release returns safely, dead/panic block new shots but pending arrows resolve.
- [x] RED replay.ranged_mid_cycle_resume: save inside4-phase cycle with arrows/future commands then compare resumed/continuous state; explicit snapshot/replay v1 rejection. RED world validation rejects0/65 cohorts. Keep old focused physics fixtures explicit cohort1 as isolation.
- [x] GREEN deterministic phase/cadence, JSON/hash/validation, schema2; optional bench --ranged-cohorts N for controlled comparison.
- [x] All Debug/Release tests pass; local commit.

## Task 4: Comparative benchmarks and delivery
**Files:** BattleCore/docs/{ranged-performance,scenario-format,benchmark-protocol,implementation-status}.md,README.md,docs/benchmarks/ranged-*.jsonl and any benchmark diagnostics corrections.
**Interfaces:** same benchmark fixture/method; --ranged-cohorts1/4 changes only the explicitly authorized rule. No worker-speedup claim.
- [x] Final Release volley1k/5k/10k/20k,3reps,200+1200 steps; cohort1 extra10k3reps; melee10k/20k3reps. Raw per-phase data and launch/hit/event counters, p50/p95/p99/max/memory.
- [x] Compare target behavior with cohort1, quantify baseline/final and mechanics differences. Target10k volley p99<50ms, aim<=20ms; report misses honestly.
- [x] End-to-end import, save/resume/replay with new schema; full Debug/Release suite; document how to regenerate and old continuation incompatibility.
- [x] Commit, mark plan complete, one fresh final review from pre-plan base. Fix Important issues with RED->GREEN and full suite, ledger minors/rulings. Keep local branch; no push/merge.
