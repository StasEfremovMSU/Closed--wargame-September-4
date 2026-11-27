#pragma once

#include "Math/UnrealMathUtility.h"
#include "Math/Vector.h"

namespace RegimentOfficerView
{
inline FVector Eyes(const FVector& Footing, bool bMounted)
{
    return Footing + FVector(0, 0, bMounted ? 219.0 : 159.0);
}

inline double Pitch(double Angle, bool bFirstPerson)
{
    return bFirstPerson ? FMath::Clamp(Angle, -74.0, 74.0) : FMath::Clamp(Angle, -63.0, 24.0);
}

inline float FieldOfView(bool bFirstPerson)
{
    return bFirstPerson ? 90.f : 76.f;
}

inline double BladeReach(double Age, double Windup)
{
    const double Draw = .45 * Windup;
    if (Age < Draw)
        return -.26 * FMath::SmoothStep(0.0, Draw, Age);
    if (Age < Windup)
        return FMath::Lerp(-.26, 1.0, FMath::SmoothStep(Draw, Windup, Age));
    return 1.0 - FMath::SmoothStep(Windup, Windup + .24, Age);
}
}
