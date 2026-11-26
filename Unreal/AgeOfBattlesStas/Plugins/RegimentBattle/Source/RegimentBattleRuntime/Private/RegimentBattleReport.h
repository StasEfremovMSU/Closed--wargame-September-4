#pragma once

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Types.h"
THIRD_PARTY_INCLUDES_END

namespace battle { struct World; }

struct FRegimentBattleCount
{
    int32 Starting = 0;
    int32 Living = 0;
    int32 Eliminated = 0;
    int32 Panicked = 0;

    int32 Fallen() const { return Starting - Living; }
};

struct FRegimentBattleReportRow
{
    FText Name;
    int32 Team = 0;
    FRegimentBattleCount Count;
};

enum class ERegimentBattleMomentKind : uint8
{
    CrestControl,
    ReserveContact,
    Rout,
    CommandVacant,
    Panic,
    Rally,
    FirstLoss,
    FirstHit,
    BattleEnded
};

struct FRegimentBattleMoment
{
    ERegimentBattleMomentKind Kind;
    battle::Tick Tick;
    FText Description;
};

struct FRegimentBattleDebriefMilestones
{
    TOptional<battle::Tick> FirstCrestControl;
    TOptional<battle::Tick> FirstReserveContact;
};

struct FRegimentBattleReport
{
    FText Mission;
    FText ObjectiveResult;
    FText SideNames[2];
    FRegimentBattleCount Sides[2];
    TArray<FRegimentBattleReportRow> Rows;
    TArray<FRegimentBattleMoment> Moments;
    battle::BattleOutcome Outcome = battle::BattleOutcome::Running;
    int32 Winner = -1;
    uint64 ElapsedSeconds = 0;
};

class FRegimentBattleReportAccumulator
{
public:
    explicit FRegimentBattleReportAccumulator(const battle::World& World);

    void Observe(const battle::World& World);
    TOptional<FRegimentBattleReport> Finish(const battle::World& World,
        const FRegimentBattleDebriefMilestones& Milestones = {});

private:
    void ObserveOrderMoment(const battle::BattleEvent& Event);
    void RememberMoment(ERegimentBattleMomentKind Kind, battle::Tick Tick, int32 Row, const FText& Message);
    void BuildTimeline(const battle::World& World, const FRegimentBattleDebriefMilestones& Milestones);

    FRegimentBattleReport Report;
    TMap<uint64, int32> RowsByRegiment;
    TMap<uint64, int32> RowsByUnit;
    TOptional<FRegimentBattleMoment> CombatMoments[int32(ERegimentBattleMomentKind::BattleEnded)];
    TSet<uint64> ConfirmedDeaths;
    bool bFinished = false;
};

struct FRegimentPlayerStats
{
    uint64 Victories = 0;
    uint64 Defeats = 0;
    uint64 Draws = 0;
    uint64 ElapsedSeconds = 0;
    uint64 Fallen = 0;
    uint64 Eliminated = 0;

    uint64 Completed() const { return Victories + Defeats + Draws; }
    void Record(const FRegimentBattleReport& Report, int32 PlayerTeam);
    void Load(const FString& Path);
    bool Save(const FString& Path) const;
    static FString SavePath();
};
