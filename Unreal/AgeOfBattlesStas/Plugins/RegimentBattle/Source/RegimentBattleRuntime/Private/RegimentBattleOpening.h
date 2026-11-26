#pragma once

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include "battle/World.h"
THIRD_PARTY_INCLUDES_END

class FRegimentBattleOpening
{
public:
    static constexpr uint32 MoveTicks = 22;
    static constexpr uint32 ColumnTick = 2 * MoveTicks;
    static constexpr uint32 ColumnHoldTicks = 10;
    static constexpr uint32 DurationTicks = 4 * MoveTicks + ColumnHoldTicks;

    explicit FRegimentBattleOpening(const battle::World& World);
    bool IsActive() const { return !Paths.IsEmpty() && ElapsedTicks < DurationTicks; }
    uint32 GetElapsedTicks() const { return ElapsedTicks; }
    void Advance(battle::World& World);

private:
    struct FPath
    {
        battle::Pose Line;
        battle::Pose Staggered;
        battle::Pose Column;
    };

    TArray<FPath> Paths;
    uint32 ElapsedTicks = 0;
};
