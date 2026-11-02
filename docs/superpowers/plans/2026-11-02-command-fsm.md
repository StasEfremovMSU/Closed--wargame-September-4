# Command FSM Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Execute all four command levels as deterministic FSMs in real battles, including their map/communication/navigation/succession/persistence dependencies.
**Architecture:** Optional Scenario command_setup creates owned World command_state. Pure brains return plans; a serial runtime delivers messages, runs due brains, applies validated proposals and delegates physical execution to the existing core.
**Tech Stack:** C++20, existing CMake/CTest/doctest/JSON; no new dependency, GUI or neural runtime.
**Spec:** ../specs/2026-11-02-command-fsm-design.md

## Global Constraints
- D:/September 4, codex/battle-core, native execution; preserve/stage out pre-existing Unity changes.
- Four existing levels and authoritative single Unit bodies; no duplicate movement/damage.
- No neural training, invented ammo/cavalry/cover mechanics or Unreal claim.
- Absent command_setup preserves legacy mode. Explicit user Orders/control retain precedence.
- 50ms ticks; deterministic IDs/order, no wall-clock choices. History/contact limit64, contact TTL100, default intervals40/20/10/5.
- New version3 persistence, explicit safe upgrade of command-free version2; complete-field validation for version3.
- Local commits only; one final independent review, no push/merge.

## Review Focus
1. Invalid graphs/teams/IDs, malformed messages and dead commanders must fail safely without phantom orders (Tasks1/6).
2. No hidden enemies through obstacles/reports from the future; missing targets/paths must report failure rather than fabricate success (Tasks1/2/6).
3. Simultaneous external orders, panic, new high-priority tasks and repeated deliveries must retain one authority and deterministic transitions (Tasks2/6).
4. Empty/dead subtrees, reserves, expired tasks and unsupported reform parameters must not strand or spam the hierarchy (Tasks2–6).
5. Save/resume inside a delivery/deployment phase must preserve state/counters/timers exactly; malformed nested state cannot default silently (Task7).

## Task 1: Working hierarchy and map services
**Files:** command/{Hierarchy,Perception,Messages,Services}.h; new command/Setup.h; src/CommandServices.cpp, new src/CommandPerception.cpp; tests/CommandTests.cpp; example temporarily updates old inactive assertions as services become real.
**Interfaces:** existing ValidateHierarchy/BuildSituation/SelectSuccessor/ScheduleBrains become ready or InvalidInput; FindNode/FindProfile and OwnedRegiments helpers; LocalSituation adds rally_point. CommandSetup{hierarchy,missions}; CommandTask adds optional formation_columns/formation_spacing_m. All persisted records gain default equality.
- [x] RED command.validation_rejects_broken_graph and command.observation_respects_range_and_obstacles; command.successor_and_schedule checks live eligible officer, dedup/due tick, malformed schedules. Observe failures against stubs.
- [x] GREEN validate graph, profiles, side/bindings; direct centre visibility via segment/rectangle intersection, own subtree summaries, sorted contacts/threat sectors/rally point; stable live succession and tick scheduling. Return typed diagnostics on empty/invalid inputs.
- [x] Run all Debug tests, commit, task-done.

## Task 2: Regiment FSM and rules evaluator
**Files:** command/Brains.h, CommandBrains.cpp; new src/CommandRegiment.cpp, src/CommandBrainCommon.{h,cpp}; tests/CommandRegimentTests.cpp; CMake test group and demo.
**Interfaces:** FsmState enum and persisted BrainMemory{state,state_entered_tick,active_task,task_status,target,goal,...existing history}; ChildStatus snapshots and BrainContext.current_formation/children_status; pure shared helpers for task selection, memory transition/report building. RulesEvaluatorStub becomes a functioning deterministic evaluator; NeuralEvaluatorStub stays NotImplemented.
- [x] RED regiment transitions: hold/move completion, attack visible/dead/unavailable target, panic override, priority/expiry/not-before, minimum commitment, autonomous initiative, flank two phases, reform input; empty/mismatched contexts return InvalidInput. Evaluator produces finite indexed scores.
- [x] GREEN implement polk states and transitions with bounded memory and reports only on changes. Task kinds map to existing actions/directives; no hidden World access.
- [x] Full Debug suite, commit, task-done.

## Task 3: Group coordination FSM
**Files:** new src/CommandGroup.cpp, tests/CommandGroupTests.cpp; Brains/common helpers as needed.
**Interfaces:** GroupBrain::Think uses child status/readiness and current task. Own reserve child is held until release; deploy wait bounded40 ticks.
- [x] RED group.deploys_then_attacks and group.reserve_support_and_withdraw: complementary child tasks, readiness/timeout, no friendly attack, panic/loss report, no children.
- [x] GREEN coordinated deployment, attack/support/regroup/withdraw/rally and child task reports; canonical ordering independent of input child order.
- [x] Full Debug suite, commit, task-done.

## Task 4: Part sector FSM
**Files:** new src/CommandPart.cpp, tests/CommandPartTests.cpp.
**Interfaces:** PartBrain::Think gives stable frontage positions, main/support/reserve assignments and upstream requests.
- [ ] RED part.frontage_reserve_and_threat and part.expiry_and_loss: distinct child goals, reserve held/released, unsupported/empty/dead subtree safe, orderly withdrawal.
- [ ] GREEN sector deployment/hold/advance/support/withdraw, deterministic offsets based on child widths/roles; respect superior intent and time limits.
- [ ] Full Debug suite, commit, task-done.

## Task 5: Army FSM
**Files:** new src/CommandArmy.cpp, tests/CommandArmyTests.cpp.
**Interfaces:** ArmyBrain::Think chooses mission or defensive/attacking fallback by visible balance and profile, assigns parts by sector roles, controls reserve.
- [ ] RED army.mission_and_autonomous_balance, army.reserve_release_and_target_loss: explicit Hold obeyed, no enemy means defensive posture, adequate forces advance, reserve release on >=25% own losses or panic, safe missing target.
- [ ] GREEN army-level task allocation and monitoring, no physical action output; contract metadata marks implemented stages accurately.
- [ ] Full Debug suite, commit, task-done.

## Task 6: Communication and live command runtime
**Files:** new command/Runtime.h, src/CommandRuntime.cpp; Scenario.h, World.h, World.cpp, Simulation.cpp, Orders.h; CommandServices.cpp; tests/CommandRuntimeTests.cpp, example.
**Interfaces:** optional command_setup in Scenario, command_state in World. NodeRuntime holds memory/active tasks/reports/schedule/last_order_id/last child assignments; CommandState owns mutable hierarchy, nodes, communication and monotonic counters. InitializeCommand(World&), UpdateCommand(World&,const NavigationService&), SubmitTask(World&,const CommandTask&). Add CommandKind::IssueCommandTask with Command.command_task.
- [ ] RED pure queue no-early/expiry/dedup, task->Order translation, invalid authority/side, four-level delivery and visible movement/combat, reserve/reports/succession, no-path failure, external-order/control precedence and no order spam. Courier route/time/death checked; no physical courier animation claimed.
- [ ] GREEN message transport and owner runtime; first1-tick abstract Signal per hop. Capture direct and aged report knowledge; run due/urgent brains; publish child tasks only on changed plan; set single leaf order owner. Wire between UpdateMorale and UpdateOfficers. Legacy scenarios without setup unchanged.
- [ ] Full Debug/Release suites and demo; commit, task-done.

## Task 7: Persistence, CLI scenario and documentation
**Files:** Snapshot.cpp/h, JsonBindings.h, new tools/CommandJson.h; ReplayIo.cpp/h; new src/CommandState.cpp; tests/{CommandRuntime,Replay}Tests.cpp, CliRejected.cmake; example, fixtures/command-battle.json, docs/command-hierarchy.md, scenario-format.md, README.md.
**Interfaces:** CommandStateHash/ValidateCommandState; strict JSON for new enums/unsigned fields/optional/variant; version3 snapshots/replays and limited v2 migration. All runtime state is persisted and hashed; v1 rejects.
- [ ] RED save/resume/replay mid-message, hash sensitivity for each state class, corrupt/missing nested field/timer/ID/immutable topology rejected, command-free v2 accepts and v2-command state rejects, CLI IssueCommandTask changes army plan.
- [ ] GREEN serialization, hash and validation; meaningful end-to-end fixture and state/trace output; demo asserts actual Ready states/actions instead of placeholder success.
- [ ] Full Debug/Release CTest, CLI continuous/resume/replay identical state hashes, small/large hierarchy timing sample (no FPS promise); update detailed docs with implemented/remaining boundaries.
- [ ] Commit, task-done, one fresh final review from6ad0eaf. One RED/GREEN Important/Critical fix pass; keep local branch.
