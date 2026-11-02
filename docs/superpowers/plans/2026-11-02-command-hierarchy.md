# Command Hierarchy Scaffold Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Add detailed, compiled placeholders for all four command levels without changing the running battle.

**Architecture:** An isolated battle::command namespace contains hierarchy, messages, perceptions, memory and immutable brain inputs. Every new computation returns an explicit NotImplemented with no output. The current regiment AI remains the only running controller.

**Tech Stack:** C++20, existing CMake/CTest/doctest, no new dependency or Unreal requirement.

**Spec:** ../specs/2026-11-02-command-hierarchy-design.md

## Global Constraints

- D:/September 4, current codex/battle-core branch; stage explicit authored files, preserve dirty Unity files.
- Four levels: Army, Part, Group, Regiment. Soldiers execute; support roles do not add command levels.
- C++20, no RTTI/exceptions in core, no new dependencies, no model loading or training.
- Step/UpdateOfficers/World/scenario/hash/snapshot/replay remain unchanged.
- Placeholder computations return NotImplemented, no payload, no orders or queue consumption.
- Descriptions of future features must distinguish contracts from operational mechanics.
- Keep work local, no merge/push. Continue native implementation as in the existing project work.

## Review Focus

1. Default placeholder results must not masquerade as success or issue a default Hold (Task1).
2. Null/empty/uninitialised contexts must remain safe while stubs are inactive (Task1).
3. Contradictory or missing hierarchy data must not be reported as validated (Task1).
4. Declared observations must not grant implemented visibility, fatigue, cavalry or morale systems (Task2).
5. New API calls must not consume queues, alter seeded battle outcomes or silently require a new save schema (Task1).

## Task 1: Typed hierarchy and inert brains/services

**Files:** Create BattleCore/include/battle/command/{Hierarchy,Perception,Messages,Brains,Services}.h, BattleCore/src/{CommandBrains,CommandServices}.cpp, BattleCore/tests/CommandTests.cpp; modify BattleCore/CMakeLists.txt to register command tests.

**Interfaces:**
- NodeId/TaskId/MessageId; Level{Army,Part,Group,Regiment}; CommandNode, OfficerProfile, CommandHierarchy.
- LocalSituation, KnownContact; CommandTask, SituationReport, MessageEnvelope, CommunicationsState.
- StubResult<T>{StubStatus status=NotImplemented, optional<T> value, string_view reason}.
- ArmyBrain/PartBrain/GroupBrain/RegimentBrain: static const BrainContract& Contract(); StubResult<DecisionProposal> Think(const BrainContext&) const.
- ITacticalEvaluator::Evaluate(const EvaluationRequest&) const -> StubResult<vector<ActionScore>>; RulesEvaluatorStub and NeuralEvaluatorStub.
- ValidateHierarchy(const CommandHierarchy&,const World&); BuildSituation(const World&,const CommandHierarchy&,NodeId,Tick); SelectSuccessor(const World&,const CommandNode&); AdvanceCommunications(const CommunicationsState&,Tick); ScheduleBrains(span<const BrainSchedule>,Tick); TranslateRegimentTask(const CommandTask&,Tick). All return typed StubResult values.

- [x] RED: command.brains_are_explicitly_inactive calls each brain for empty and populated contexts; no output, no false success. command.services_do_not_validate_or_consume covers malformed hierarchy, queued tasks and messages, schedule and translation; no output. command.scaffold_does_not_change_battle compares seeded Step/StateHash with and without intervening stub calls. Build fails because new public API is missing.
- [x] GREEN: implement documented data structures, four static stage contracts, inactive brains/evaluators/services. Only metadata and NotImplemented returns, no tactical implementation or runtime integration.
- [x] Build Debug and run full CTest; inspect output, no failures.
- [x] Commit explicit files and record task completion.

## Task 2: Detailed reference and executable example

**Files:** Create BattleCore/examples/CommandHierarchyDemo.cpp, BattleCore/docs/command-hierarchy.md; modify BattleCore/CMakeLists.txt and BattleCore/README.md.

**Consumes:** Task1 public API; existing CreateWorld and OfficerSpec/UnitSpec.

**Produces:** battle_command_demo executable and CTest battle.command-demo; no GUI, no neural runtime.

- [x] RED: register demo target/test before adding the source; configure fails with the missing example source.
- [x] GREEN: example constructs one Army/Part/Group/Regiment chain with four distinct officers anchored in existing Unit bodies, calls every placeholder, exits nonzero if any claims success or emits a payload. Print each level and pending services.
- [x] Write Russian reference: responsibilities/inputs/outputs at all levels, officer cycle and traits, task/report/delivery contracts, succession, scheduling, ownership and future invariants, full file map and example commands. Clearly mark every unimplemented mechanic and current legacy boundary.
- [x] Build Debug/Release and full CTest including example; run example manually and inspect its output. Check formatting/diff.
- [ ] Commit, complete ledger and perform one fresh final review of the current task range. Address Important/Critical with RED/GREEN and full suites. Keep local branch.
