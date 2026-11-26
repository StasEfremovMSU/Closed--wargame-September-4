#pragma once

#include "Containers/Deque.h"
#include "Math/Transform.h"

inline constexpr uint64 RegimentFallenLifetimeTicks = 160;

struct FRegimentFallenProxy
{
    FVector Location;
    double Yaw;
    bool bMounted;
    uint64 ExpiresAt;
};

class FRegimentFallenProxies
{
public:
    static constexpr int32 MaxBodies = 160;

    void Add(FVector Location, double Yaw, bool bMounted, uint64 Tick)
    {
        if (Bodies.Num() == MaxBodies)
            Bodies.PopFirst();
        Bodies.PushLast({Location, Yaw, bMounted, Tick + RegimentFallenLifetimeTicks});
        bChanged = true;
    }

    bool TakeChanges(double Tick)
    {
        while (!Bodies.IsEmpty() && Tick >= double(Bodies.First().ExpiresAt))
        {
            Bodies.PopFirst();
            bChanged = true;
        }

        const bool bRebuild = bChanged;
        bChanged = false;
        return bRebuild;
    }

    const TDeque<FRegimentFallenProxy>& GetBodies() const { return Bodies; }

private:
    TDeque<FRegimentFallenProxy> Bodies;
    bool bChanged = false;
};
