#pragma once

#include "Math/UnrealMathUtility.h"
#include "Math/Vector.h"

namespace RegimentFieldGun
{
inline constexpr double MuzzleForwardCm = 148.0;
inline constexpr double MuzzleHeightCm = 99.0;
inline constexpr double MaxRecoilCm = 17.0;

inline FVector MuzzleOffset()
{
    return {MuzzleForwardCm, 0.0, MuzzleHeightCm};
}

inline FVector MuzzleAt(const FVector& Base, double YawRadians)
{
    return Base + FVector(FMath::Cos(YawRadians) * MuzzleForwardCm,
        FMath::Sin(YawRadians) * MuzzleForwardCm, MuzzleHeightCm);
}

inline double RecoilAt(double AgeTicks)
{
    constexpr double RecoveryTicks = 9.0;
    return AgeTicks >= 0.0 && AgeTicks < RecoveryTicks
        ? MaxRecoilCm * FMath::Square(1.0 - AgeTicks / RecoveryTicks) : 0.0;
}
}
