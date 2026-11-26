#pragma once

#include "RegimentMissionScenario.h"

enum class EMissionAuthoringIssue : uint8 { Overlap, Obstructed, OutOfBounds, Relocated, EmptyFormation };

struct FMissionAuthoringDiagnostic
{
    EMissionAuthoringIssue Kind;
    int32 Slot = INDEX_NONE;
    int32 OtherSlot = INDEX_NONE;
    FText Message;
};

struct FMissionAuthoringSpawn
{
    int32 Slot;
    int32 Group;
    FString Name;
    battle::Pose Requested;
    battle::Pose Actual;
    battle::FormationFootprint Size;
};

struct FMissionAuthoringPreview
{
    battle::MarchField Field;
    TArray<battle::Obstacle> Terrain;
    TArray<FMissionAuthoringSpawn> Spawns;
    TArray<FMissionAuthoringDiagnostic> Diagnostics;
    std::optional<battle::TerrainHill> Hill;
    std::optional<battle::TerrainCrest> Crest;
    std::optional<battle::ScenarioObjective> Objective;
    bool bTactical = false;
};

TOptional<FMissionAuthoringPreview> BuildMissionAuthoringPreview(const FMissionPreset&, FText& OutError);
