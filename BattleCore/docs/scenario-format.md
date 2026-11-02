# Scenario schema 1

UTF-8 JSON object containing `schema_version: 1`, `regiments` and `units` arrays; optional `officers`, `obstacles`, `rules`. See the self-contained `fixtures/duel.json`, `archers.json`, and `bottleneck.json` examples.

IDs are nonzero unsigned integers and independent of array indices. Each Unit references one Regiment; each Officer references one existing Unit. The same officer body is counted once. Unit fields include pose, health, speed, armour, archer_skill [0,1], equipment IDs and weapon damage/range/cooldown/windup/projectile speed/base accuracy [0,1]. Regiment fields include team, pose, formation columns/spacing/body radius, speed, turn rate, and rally point. Coordinates are metres, X forward, Y right, Z up; yaw radians.

Legacy import reads `item.txt`, `soldiers.txt`, `officer.txt` and the explicitly selected map. Blank lines and lines starting with `!` are ignored. Numeric fields and composition totals are checked, failures include the original file/line. Original inputs are never modified.

Unity (x,y,z) becomes core (z,x,y); legacy yaw is retained, default scale 1 metre/unit. Legacy regiment 0 becomes internal RegimentId 1. Officer IDs are assigned by row, retaining repeated original values as metadata with warnings. Item lookup uses each item's declared ID. Legacy health defaults to 1, ranged range to 30m, cooldown to 20 ticks. Accuracy/skill zeroes are preserved rather than silently balanced. Team rally points initially use the average of their initial regiment centres.

The backup map has 200 rank-and-file and 3 officer bodies, total 203 living Units, all on side 1. Synthetic two-army fixtures are separate data.

```powershell
./out/build/debug/battle_cli.exe import-legacy --source fixtures/legacy --map map-backup.txt --out out/legacy.json
./out/build/debug/battle_cli.exe run --scenario fixtures/duel.json --seed 42 --ticks 100 --out out/duel
```

Run writes an owned snapshot and a JSON-lines event trace. Invalid input exits 2 with diagnostics; successful execution exits 0.

## Ranged scheduling and continuation version2

Scenario authoring remains schema1. Optional `rules.ranged_cohorts` defaults to4 and must be1..64. AI archers attempt initial/idle target acquisition in their stable UnitId/RegimentId phase; four phases spread attempts over200ms with at most150ms initial wait. A nonzero exact `ready_tick` is always eligible once cooldown finishes, so arbitrary cooldown periods retain sustained cadence. Idle retries use the next phase. PlayerControlled archers bypass phase scheduling; their weapon cooldown still applies. Set cohorts1 for simultaneous AI acquisition/release. Dead/panicked units cannot start attacks; already launched projectiles remain independent.

Snapshots and recorded replay bundles now use schema2, including the ranged rule. The current reader explicitly rejects version1 continuation files. Keep the preserved baseline executable for old files or start a new run from a scenario; do not merely relabel an old save as version2. Scenario defaults and authored `--commands` files remain distinct from fully persisted continuation records. Derived phases need no new per-unit scheduler field; stable IDs, the configured rule and existing ready_tick are sufficient for save/resume.

Benchmark comparison: `bench --units 10000 --modes volley --ranged-cohorts 1|4 --ticks 1200 --warmup 200 --out DIR`. The flag is benchmark-only; normal runs read the rule from the scenario.
