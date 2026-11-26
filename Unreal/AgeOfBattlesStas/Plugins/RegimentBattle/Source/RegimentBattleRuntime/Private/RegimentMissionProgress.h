#pragma once

#include "Containers/Set.h"
#include "Math/Color.h"
#include "Misc/Optional.h"
#include "RegimentMissionPreset.h"
#include "RegimentMissionTutorial.h"

THIRD_PARTY_INCLUDES_START
#include "battle/World.h"
THIRD_PARTY_INCLUDES_END

class FRegimentMissionProgress
{
public:
    FRegimentMissionProgress(const FMissionPreset& Preset, battle::World& World);

    void Update(battle::World& World, bool bOfficerCommandsOpen = false);
    bool StartsPossessed() const { return (Tutorial && Tutorial->StartsPossessed()) || IsHillDefense(); }
    uint64 InitialCommander() const { return ReserveCommander; }
    bool IsHillDefense() const { return Objective.Goal == EMissionGoal::HillDefense; }
    FLinearColor CrestTint() const;
    bool ShowsCrest() const { return IsCrestObjective(Objective.Goal) && Result.IsEmpty(); }
    bool HidesRegiment(battle::RegimentId Regiment) const
    {
        return Tutorial && Tutorial->HidesRegiment(Regiment);
    }
    std::optional<battle::ScenarioObjective> TutorialMarker() const
    {
        return Tutorial ? Tutorial->Marker() : std::nullopt;
    }
    bool IsReserveExercise() const { return Objective.Goal == EMissionGoal::ReserveExercise; }
    bool HasReserve() const { return bool(Reserve); }
    bool CanReleaseReserve(const battle::World& World) const;
    bool AwaitsReserveRelease(battle::RegimentId Regiment) const;
    void ReleaseReserve(battle::World& World);
    FText Status(const battle::World& World) const;
    FText Report() const;

private:
    void Finish(battle::World& World, int32 Winner, FText Explanation);
    void UpdateHillHold(battle::World& World, battle::Tick Deadline);
    void ObserveReserve(const battle::World& World);
    void UpdateBreakthrough(battle::World& World, battle::Tick Deadline);

    FMissionObjective Objective;
    battle::MarchTerrain Terrain;
    TOptional<FRegimentTutorialProgress> Tutorial;
    FText Result;
    TArray<battle::RegimentId> StartsOnAttackingBank;
    TSet<uint64> UsedCrossing;
    TSet<uint64> Passed;
    TArray<battle::RegimentId> BreakthroughInfantry;
    int32 InBreakthrough = 0;
    battle::RegimentId Lead{}, Reserve{};
    battle::OrderId ReleaseOrder{};
    uint64 ReserveCommander = 0;
    bool bReserveAtLine = false;
    bool bReserveOrderFailed = false;
    TOptional<battle::Tick> Issued, Delivered, FirstContact;
    uint64 LeadInitial = 0;
    uint64 LeadAtContact = 0;
    TOptional<battle::Tick> LastHillSample, FirstHillControl;
    battle::Tick HillHeldTicks = 0;
    battle::Tick BestHillHeldTicks = 0;
    bool bHillContested = false;
    bool bBlueOnCrest = false;
    battle::Tick HillLostTicks = 0;
};
