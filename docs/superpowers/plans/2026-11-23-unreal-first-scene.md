# First Unreal battle scene implementation plan

> **For agentic workers:** Use superpowers:executing-plans. Steps use checkbox syntax.

**Goal:** Open a separate AgeOfBattlesStas level and watch 100 BattleCore units move in formation through real Mass entities.

**Architecture:** A runtime plugin links the MSVC-built BattleCore static library and owns one battle session per placed field actor. Each completed simulation frame updates one Mass entity and one visual component per living unit. A scene creation script builds a level from engine primitives and the Fab models already in the project.

**Tech Stack:** UE 5.8.4, MassEntity and MassGameplay, Win64, MSVC 14.44 and Windows SDK 10.0.22621.0, C++20, Unreal Editor Python for scene preparation.

**Spec:** [Unreal adapter contract](../../../BattleCore/docs/unreal-adapter-contract.md), first integration milestone in Task 12.

## Constraints

- Work in the existing Unreal project at `D:/Unreal Projects/AgeOfBattlesStas`. The Git checkout at `D:/September 4` supplies BattleCore.
- The first level uses a flat playable surface and existing BattleCore navigation. The tactical-map schema is a separate design.
- Use stable `CoreUnitId` values and validate Mass handles. Convert metres to centimetres once. Advance a fixed 50 ms simulation step.
- Keep imported Fab asset files in the Unreal project.

## Tasks

- [x] Build `battle_core` for x64 Release with MSVC. Put the build under `BattleCore/out/build/ue-msvc`.
- [x] Write and run `Scripts/Check-RegimentBattle.ps1`. It checks the field map, starts an isolated headless game, and asserts that the map produces 100 unique Mass entities, 100 figures, eight command nodes and advancing unit positions after 100 core ticks. The initial missing map is the expected failure.
- [x] Add `Plugins/RegimentBattle` with the `BattleCoreExternal` and `RegimentBattleRuntime` modules. The field actor starts two 50-unit regiments with command hierarchies and moves their Mass presentations. It removes Mass entities on teardown.
- [x] Use `Scripts/CreateRegimentField.py` to build `/Game/Battle/Maps/RegimentField` with a preview of the field, overhead camera, lighting and a placed field actor. Save the level separately from the editor's current level.
- [x] Build the runtime plugin through UBT, write UBT-generated module manifests, run the headless integration check, and review the adapter and failure paths. Record the level path and what was exercised.

## Result

UE 5.8.4 opened the authored level and completed the 100-tick integration check in `-game -nullrhi`. Both checkpoints had 100 valid Mass entities and 100 registered figures in agreement with the core poses. Tick 100 retained the initial handles. The front sample of each side moved from ±1480 cm to ±25 cm. The Unreal project uses the new map as its startup level.

The open editor locked the main project's DLL. The new module was built using UBT's module target, then UBT wrote the generated metadata. This run checked the actual scene and simulation without rendering. Rendering, animation, possession, packaging and the new tactical-map format have separate acceptance work.
