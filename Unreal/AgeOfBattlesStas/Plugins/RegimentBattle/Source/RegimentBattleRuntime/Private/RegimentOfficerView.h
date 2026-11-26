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
}
