#pragma once

#include "CoreTypes.h"

enum class ERegimentBattleSpeed : uint8
{
    Half, Normal, Double
};

class FRegimentBattlePlayback
{
public:
    static constexpr double StepSeconds = 0.05;
    static constexpr uint64 StepsPerSecond = 20;

    void AddTime(double DeltaSeconds, int32 MaxBacklogSteps = 0)
    {
        if (IsPaused())
            return;
        const double Rate = Speed == ERegimentBattleSpeed::Half ? 0.5
                          : Speed == ERegimentBattleSpeed::Double ? 2.0 : 1.0;
        Accumulator += Rate * DeltaSeconds;
        const double MaxBacklog = MaxBacklogSteps * StepSeconds;
        if (MaxBacklogSteps > 0 && Accumulator > MaxBacklog)
            Accumulator = MaxBacklog;
    }

    bool TakeStep()
    {
        if (IsPaused() || Accumulator < StepSeconds)
            return false;
        Accumulator -= StepSeconds;
        return true;
    }

    bool IsPaused() const { return bPaused || bOfficerSelection; }
    bool IsSelectingOfficer() const { return bOfficerSelection; }
    void SetOfficerSelection(bool bSelecting) { bOfficerSelection = bSelecting; }
    void SetPaused(bool bInPaused) { bPaused = bInPaused; }
    ERegimentBattleSpeed GetSpeed() const { return Speed; }
    void SetSpeed(ERegimentBattleSpeed InSpeed) { Speed = InSpeed; }
    void ResetClock() { Accumulator = 0.0; }

private:
    double Accumulator = 0.0;
    ERegimentBattleSpeed Speed = ERegimentBattleSpeed::Normal;
    bool bPaused = false;
    bool bOfficerSelection = false;
};
