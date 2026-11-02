# Autonomous delivery status

Tasks 1–11 are implemented in `D:/September 4/BattleCore`, branch `codex/battle-core`. The original Unity content and pre-existing uncommitted edits are preserved. Task 12 (an actual Unreal/Mass host) awaits an installed engine and selected UE-compatible toolchain.

## Validation

- Debug and Release: 12/12 CTest groups pass, including CLI subprocess tests with bounded rejection cases.
- Legacy fixture imports as 7 regiments, 203 living units, one side; no synthetic opposing army is inserted.
- Save/resume, continuous run and recorded replay produce identical complete snapshots at tick200, hash `5776692489466754327`; final corrected code reproduces this state.
- Six top-down SVGs were XML-parsed, rasterized and visually inspected for formation geometry and bottleneck traversal.
- 120 baseline/optimized benchmark runs plus 12 obstacle follow-up runs are recorded in [results](benchmark-results.md). Matching hashes demonstrate preserved results for these fixtures.
- A fresh read-only review by a separate gpt-6-astra reviewer found five Important issues and two originally labelled Minor. Numeric CLI parsing was regraded Important because a typo could start unbounded work; volley-tail disclosure was regraded Important because it changes capacity interpretation. Every executable correction below had a failing regression before implementation, followed by the green full suite. No second review was claimed.

## Review corrections

| Finding | Verification |
|---|---|
| Rejected Attack replaced an active order | `officers.rejected_attack_preserves_active_order` |
| Returning/reforming soldier penetrated a static wall | `control.returning_and_reforming_respect_static_walls` |
| Missing nested snapshot fields silently took defaults | `replay.rejects_missing_nested_continuation_fields` |
| Runtime immutable definitions diverged from the embedded scenario | `replay.immutable_definitions_match_source` |
| Resume exported an unrelated scenario-based replay | `battle.cli`: unsupported combination rejected before outputs |
| Malformed/negative/overflow counters accepted | `battle.cli`: full decimal token/range rejection before outputs |
| Passing p95 obscured severe synchronized-volley bursts | README/results explicitly disclose 10k volley p99≈225ms, beyond the 50ms tick |

Scenario authoring retains documented defaults; snapshot continuation requires every serialized field recursively. No gameplay command currently changes team, weapons, equipment or officer definitions; restored values must agree with the source scenario. Pose/health and supported formation changes remain mutable.

## Decisions and their costs

1. Use the user-selected D copy on a new local branch without another worktree. Existing dirty Unity files coexist, so only explicit BattleCore/docs paths were staged.
2. Close ranks explicitly after casualty commit, preserving IDs and actual positions. Survivors adjust target slots instead of retaining empty places indefinitely.
3. Prepare the presentation contract while benchmarks run, close Task10 before Task11. Running binaries stay immutable; a simulation change requires repeating affected measurements. The final collision correction was remeasured at all four sizes.
4. Stop individual Returning/slot movement conservatively at static obstacles. Individual detour routing is not implemented; a soldier may remain Returning until its target becomes reachable.
5. Defer real Mass/UE, packaged rendering, Pawn physics and engine navigation to Task12, as approved. Engine-specific compile/runtime issues can still require changes there.

Deferred minor review findings: none. The serial implementation and synchronized-volley tail remain explicit performance limitations, rather than claims of a finished real-time game. The [Mass contract](unreal-adapter-contract.md) is tested with a mock; it does not prove UE API compatibility.
