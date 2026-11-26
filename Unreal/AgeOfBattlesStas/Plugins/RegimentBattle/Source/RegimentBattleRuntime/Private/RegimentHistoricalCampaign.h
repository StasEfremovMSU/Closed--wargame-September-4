#pragma once

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include "battle/World.h"
THIRD_PARTY_INCLUDES_END

namespace RegimentHistoricalCampaign
{
struct FStage
{
    FGuid MissionId;
    FText Label;
};

struct FCampaign
{
    FString PlayerFaction;
    FText Title;
    FText Description;
    TArray<FStage> Stages;
};

struct FRun
{
    int32 Campaign;
    int32 Stage;
};

const TArray<FCampaign>& Catalog();
FString SavePath();

class FProgress
{
public:
    bool IsComplete(int32 Campaign, int32 Stage) const;
    bool IsUnlocked(int32 Campaign, int32 Stage) const;
    bool RecordOutcome(TOptional<FRun> Run, battle::BattleOutcome Outcome, int32 Winner);
    void Load(const FString& Path);
    bool Save(const FString& Path) const;

private:
    TSet<FGuid> Victories;
};
}
