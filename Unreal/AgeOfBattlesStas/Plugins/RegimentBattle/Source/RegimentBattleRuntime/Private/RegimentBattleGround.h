#pragma once

#include "Math/Vector.h"
#include "Misc/Optional.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Terrain.h"
THIRD_PARTY_INCLUDES_END

// The simulation stores planar positions. Picking uses the same surface as the rendered feet.
inline TOptional<FVector> RegimentGroundPoint(const FVector& RayOrigin, const FVector& Ray,
    const FVector& FieldOrigin, const std::optional<battle::TerrainHill>& Hill)
{
    if (FMath::IsNearlyZero(Ray.Z))
        return {};
    const double Base = (FieldOrigin.Z - RayOrigin.Z) / Ray.Z;
    if (!Hill)
        return Base > 0.0 ? TOptional<FVector>(RayOrigin + Ray * Base) : TOptional<FVector>{};
    if (Ray.Z >= 0.0 || Base <= 0.0)
        return {};

    constexpr double Cm = 100.0;
    const auto GroundClearance = [&](double Distance)
    {
        const FVector Position = (RayOrigin + Ray * Distance - FieldOrigin) / Cm;
        return Position.Z - battle::HillHeight(*Hill, {float(Position.X), float(Position.Y), 0.f});
    };
    double Start = FMath::Max(0.0, Base + Cm * Hill->height_m / Ray.Z);
    double End = Base;
    const auto ClipAxis = [&](double Position, double Direction, double Radius)
    {
        if (FMath::IsNearlyZero(Direction))
            return FMath::Abs(Position) <= Radius;
        const double A = (-Radius - Position) / Direction;
        const double B = (Radius - Position) / Direction;
        Start = FMath::Max(Start, FMath::Min(A, B));
        End = FMath::Min(End, FMath::Max(A, B));
        return Start <= End;
    };
    const FVector Relative = RayOrigin - FieldOrigin;
    if (!ClipAxis(Relative.X, Ray.X, Hill->radius_x_m * Cm) ||
        !ClipAxis(Relative.Y, Ray.Y, Hill->radius_y_m * Cm))
        return RayOrigin + Ray * Base;

    if (GroundClearance(Start) <= 0.0)
        return RayOrigin + Ray * Start;

    double Clear = Start;
    constexpr int32 TerrainSamples = 32;
    for (int32 Index = 1; Index <= TerrainSamples; ++Index)
    {
        double Hit = FMath::Lerp(Start, End, double(Index) / TerrainSamples);
        if (GroundClearance(Hit) > 0.0)
        {
            Clear = Hit;
            continue;
        }
        for (int32 Refinement = 0; Refinement < 12; ++Refinement)
        {
            const double Mid = (Clear + Hit) * .5;
            if (GroundClearance(Mid) > 0.0)
                Clear = Mid;
            else
                Hit = Mid;
        }
        return RayOrigin + Ray * Hit;
    }
    return RayOrigin + Ray * Base;
}