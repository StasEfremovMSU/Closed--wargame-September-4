#pragma once

#include "Containers/Set.h"
#include "RegimentMissionPreset.h"

THIRD_PARTY_INCLUDES_START
#include "battle/World.h"
THIRD_PARTY_INCLUDES_END

namespace RegimentMissionTutorial
{
struct FLesson
{
    ERegimentTutorial Id;
    FText Name;
    FText Description;
};

TConstArrayView<FLesson> Lessons();
bool Load(ERegimentTutorial Id, FMissionPreset& Out);
void PrepareScenario(ERegimentTutorial Id, battle::Scenario& Scenario);
}

class FRegimentTutorialProgress
{
public:
    FRegimentTutorialProgress(ERegimentTutorial Id, battle::World& World);

    bool StartsPossessed() const;
    bool AwaitsReserveRelease(battle::RegimentId Regiment) const;
    bool HidesRegiment(battle::RegimentId Regiment) const;
    void Update(battle::World& World, bool bOfficerCommandsOpen);
    FText Status() const;
    std::optional<battle::ScenarioObjective> Marker() const { return TargetZone; }

private:
    enum class EStep : uint8
    {
        InfantryMove, InfantryFollow, InfantryColumn, InfantryCross, InfantryLine, InfantryShoot,
        InfantryEnemyApproaches, InfantryAttack, InfantryWithdraw,
        CavalryPossess, CavalryFlank, CavalryFlankAttack, CavalryRear, CavalryAttack, CavalryWithdraw, CavalryLoss,
        ReservePossess, ReserveLine, ReserveShoot,
        ArtilleryShoot,
        GroupPossess, GroupPanel, GroupOrder, ArmyPossess, ArmyPanel, ArmyOrder
    };

    void Advance(EStep Next, const battle::World& World);
    void Finish(battle::World& World);
    void Fail(battle::World& World);
    void UpdateInfantry(battle::World& World);
    void UpdateCavalry(battle::World& World);
    void UpdateArtillery(battle::World& World);
    void UpdateCommand(battle::World& World, bool bOfficerCommandsOpen);
    FText Instruction() const;

    ERegimentTutorial Lesson;
    EStep Step;
    battle::Tick StepStarted = 0;
    std::optional<battle::Vec3> StartLocation;
    std::optional<battle::ScenarioObjective> TargetZone;
    TSet<uint64> CrossedBridge;
    TSet<uint64> ArtilleryLaunches;
    bool bRearAttackOrdered = false;
    FText Result;
};
