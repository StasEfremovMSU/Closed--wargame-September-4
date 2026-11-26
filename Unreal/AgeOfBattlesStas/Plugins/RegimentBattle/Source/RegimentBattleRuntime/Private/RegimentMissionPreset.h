#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Internationalization/Text.h"
#include "Misc/Guid.h"
#include "RegimentMissionRoster.h"

THIRD_PARTY_INCLUDES_START
#include "battle/MarchMission.h"
THIRD_PARTY_INCLUDES_END

enum class EMissionGoal : uint8 { Eliminate, Crossing, ReserveExercise, HillHold, Breakthrough };
inline constexpr float MissionStartMaxX = battle::MarchHalfWidth - 4.f;
inline constexpr float MissionStartMaxY = battle::MarchHalfHeight - 4.f;
inline constexpr int32 MissionMinTimeLimit = 10;
inline constexpr int32 MissionHillHoldSeconds = 20;
inline constexpr int32 MissionMaxTimeLimit = 600;
inline constexpr int32 MissionMinFieldWidth = 80;
inline constexpr int32 MissionMaxFieldWidth = 600;
inline constexpr int32 MissionMinFieldHeight = 60;
inline constexpr int32 MissionMaxFieldHeight = 400;
inline constexpr int32 MissionStandardFigureTarget = 5000;
inline constexpr int32 MissionMaxFigureTarget = 10000;
inline const FGuid MissionSmokeExerciseId(0x8A5DF38B, 0x7650418A, 0x96349670, 0x0BAFEC20);

struct FMissionBreakthrough
{
    float X = 0.f;
    float Y = 0.f;
    float Radius = 0.f;
    int32 Group = 2;
};

struct FMissionObjective
{
    EMissionGoal Goal = EMissionGoal::Eliminate;
    int32 TimeLimitSeconds = 90;
    int32 Crossing = 1;
    int32 RequiredRegiments = 1;
    int32 CrossingTeam = 0;
    TOptional<FMissionBreakthrough> Breakthrough;
};

inline bool IsMissionBreakthroughInfantry(int32 Slot, const FMissionRosterEntry& Entry, int32 Group)
{
    return Slot < MissionRegimentsPerTeam && Entry.bEnabled && !Entry.bCavalry &&
        Entry.Platform == battle::BattlePlatform::None && MissionGroup(Slot, Entry) == Group &&
        Entry.Total() > MissionCommandStaffCount;
}

struct FMissionEraSelection
{
    FString Id;
    int32 Version = 0;
    FString BlueFaction;
    FString OrangeFaction;

    bool IsSet() const { return !Id.IsEmpty(); }
};

struct FMissionPreset
{
    FGuid EditableCopyId;
    FString Name;
    FMissionRoster Roster = MakeDefaultMissionRoster();
    battle::MarchTerrain Terrain = battle::MarchTerrain::River;
    battle::MarchField Field;
    int32 FigureTarget = 0;
    FMissionObjective Objective;
    FString Briefing;
    TArray<FString> Sources;
    bool bRegimentTactics = true;
    bool bRiskyVolley = false;
    bool bDirectDeployment = false;
    bool bAdaptiveBattle = false;
    FMissionEraSelection Era;
    bool bSmokeExercise = false;
    FGuid HistoricalIdentity; // Assigned by the built-in catalogue.

    bool StartsInBattle() const { return bDirectDeployment || Objective.Goal != EMissionGoal::Eliminate; }
};

struct FMissionPresetListItem
{
    FGuid Id;
    FString Name;
    bool bBuiltIn = false;
    FString EraId;
    bool bSharedAcrossModules = false;
    bool bHistorical = false;
    bool bPlayableExercise = false;
};

struct FRegimentEraDefinition;

namespace RegimentMissionPresets
{
TOptional<battle::MarchTerrain> FindTerrain(FStringView Name);
const TCHAR* TerrainKey(battle::MarchTerrain Terrain);
FString Directory();
TArray<FMissionPresetListItem> BuiltIns();
bool LoadBuiltIn(FGuid Id, FMissionPreset& OutPreset);
void ApplyEra(FMissionPreset& Preset, const FRegimentEraDefinition& Era);
bool Validate(const FMissionPreset& Preset, FText& OutError);
TArray<FMissionPresetListItem> List(const FString& Directory);
TArray<FMissionPresetListItem> ListForEra(const FString& Directory, FStringView EraId);
bool Load(const FString& Directory, FGuid Id, FMissionPreset& OutPreset, FText& OutError);
bool Save(const FString& Directory, FGuid Id, const FMissionPreset& Preset, FText& OutError);
bool SaveCopy(const FString& Directory, const FMissionPreset& Preset, FGuid& OutId, FText& OutError);
}
