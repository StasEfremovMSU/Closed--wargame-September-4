#pragma once

#include "CoreMinimal.h"
#include "battle/World.h"

class FRegimentFieldFeedback
{
public:
    void ObserveAttack(const battle::BattleEvent& Event);
    FText FlagState(const battle::RegimentState& Regiment, uint64 Tick) const;
    static battle::UnitId CardOfficer(const battle::World& World, battle::RegimentId Regiment, uint64 Node);
    static bool QueueCardOfficer(const battle::World& World, battle::RegimentId Regiment, uint64 Node,
        std::vector<battle::CommandEnvelope>& Pending, uint64& Sequence);

private:
    TMap<uint64, uint64> LastShot;
};
