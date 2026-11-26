#pragma once

#include "RegimentMissionRoster.h"
#include "Math/Quat.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"

THIRD_PARTY_INCLUDES_START
#include "battle/MarchMission.h"
THIRD_PARTY_INCLUDES_END

struct FRegimentView
{
    FVector Location;
    FRotator Rotation;
};

class FRegimentMapFlight
{
public:
    static constexpr double IntroSeconds = 1.6;
    static constexpr double OrbitSeconds = 18.0;

    void Start(const FVector& Location, const FRotator& Rotation, const FVector& FieldOrigin, battle::MarchField Field)
    {
        Initial = {Location, Rotation};
        Origin = FieldOrigin;
        Radius = FVector2D(Field.HalfWidth() * 120.0, Field.HalfHeight() * 120.0);
        Altitude = Radius.Size() * .78;
        const FVector Offset = Location - Origin;
        StartAngle = Offset.IsNearlyZero(1.0) ? -UE_DOUBLE_PI * .5 :
            FMath::Atan2(Offset.Y / Radius.Y, Offset.X / Radius.X);
        Elapsed = 0.0;
        bActive = true;
    }

    bool IsActive() const { return bActive; }
    void Stop() { bActive = false; }

    FRegimentView Sample(double Seconds) const
    {
        const double Travel = FMath::SmoothStep(0.0, 1.0, (Seconds - IntroSeconds) / OrbitSeconds);
        const double Angle = StartAngle + 2.0 * UE_DOUBLE_PI * Travel;
        const FVector Position = Origin + FVector(Radius.X * FMath::Cos(Angle), Radius.Y * FMath::Sin(Angle), Altitude);
        const FRotator LookAt = (Origin - Position).Rotation();
        if (Seconds >= IntroSeconds)
            return {Position, LookAt};

        const double Blend = FMath::SmoothStep(0.0, IntroSeconds, Seconds);
        return {FMath::Lerp(Initial.Location, Position, Blend),
            FQuat::Slerp(Initial.Rotation.Quaternion(), LookAt.Quaternion(), Blend).Rotator()};
    }

    FRegimentView Advance(double DeltaSeconds)
    {
        Elapsed = FMath::Min(Elapsed + DeltaSeconds, IntroSeconds + OrbitSeconds);
        bActive = Elapsed < IntroSeconds + OrbitSeconds;
        return Sample(Elapsed);
    }

private:
    FRegimentView Initial = {FVector::ZeroVector, FRotator::ZeroRotator};
    FVector Origin = FVector::ZeroVector;
    FVector2D Radius = FVector2D::ZeroVector;
    double Altitude = 0.0;
    double StartAngle = 0.0;
    double Elapsed = 0.0;
    bool bActive = false;
};

inline FVector RegimentFlightCameraLocation(FVector Position, const FVector& Origin,
    battle::MarchField Field, const std::optional<battle::TerrainHill>& Hill)
{
    const FVector Meters = (Position - Origin) / 100.0;
    const double Ground = Hill ? 100.0 * battle::HillHeight(*Hill, {float(Meters.X), float(Meters.Y), 0.0f}) : 0.0;
    const double HeightLimit = 13000.0 * FMath::Max(1.0, MissionCameraScale(Field));
    Position.Z = Origin.Z + FMath::Clamp(Position.Z - Origin.Z, Ground + 150.0, HeightLimit);
    return Position;
}

inline FVector RegimentCameraZoomTarget(const FVector& Camera, const FVector& Anchor, double Steps,
    const FVector& Origin, battle::MarchField Field, const std::optional<battle::TerrainHill>& Hill)
{
    constexpr double ScalePerStep = .78;
    const FVector Desired = Anchor + (Camera - Anchor) * FMath::Pow(ScalePerStep, FMath::Clamp(Steps, -10., 10.));
    const auto Fits = [&](const FVector& Position)
    {
        return RegimentFlightCameraLocation(Position, Origin, Field, Hill).Equals(Position, .001);
    };
    if (Fits(Desired))
        return Desired;

    // Stop along the viewing ray so the cursor does not slide at either height limit.
    double Clear = 0.0;
    double BeyondLimit = 1.0;
    for (int32 Probe = 0; Probe < 16; ++Probe)
    {
        const double Fraction = (Clear + BeyondLimit) * .5;
        if (Fits(FMath::Lerp(Camera, Desired, Fraction)))
            Clear = Fraction;
        else
            BeyondLimit = Fraction;
    }
    return FMath::Lerp(Camera, Desired, Clear);
}
