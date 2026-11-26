#pragma once

#include <algorithm>

#include "Camera/CameraModifier.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/PlatformTime.h"
#include "RegimentArtilleryCameraKick.generated.h"

class FRegimentArtilleryKickState
{
public:
    void Trigger(double DistanceCm, double TimeSeconds)
    {
        constexpr double FullStrengthCm = 400.0;
        constexpr double FarLimitCm = 2500.0;
        constexpr double MaximumPitchDegrees = 1.2;
        const double Proximity = std::clamp((FarLimitCm - DistanceCm) / (FarLimitCm - FullStrengthCm), 0.0, 1.0);
        const double Pitch = MaximumPitchDegrees * Proximity * Proximity;
        if (Pitch > 0.0 && (Pitch > PeakPitch || PitchDegrees(TimeSeconds) == 0.0))
        {
            PeakPitch = Pitch;
            LaunchTime = TimeSeconds;
        }
    }

    double PitchDegrees(double TimeSeconds) const
    {
        constexpr double DurationSeconds = .24;
        const double Remaining = std::clamp(1.0 - (TimeSeconds - LaunchTime) / DurationSeconds, 0.0, 1.0);
        return PeakPitch * Remaining * Remaining;
    }

    bool HasKick() const { return PeakPitch > 0.0; }

    void Reset() { PeakPitch = 0.0; }

private:
    double PeakPitch = 0.0;
    double LaunchTime = 0.0;
};

UCLASS()
class URegimentArtilleryCameraKick final : public UCameraModifier
{
    GENERATED_BODY()

public:
    void KickFrom(const FVector& GunLocation)
    {
        if (CameraOwner)
            State.Trigger(FVector::Dist(CameraOwner->GetCameraLocation(), GunLocation), FPlatformTime::Seconds());
    }

    void ResetKick() { State.Reset(); }

    bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override
    {
        if (State.HasKick())
        {
            const double Pitch = State.PitchDegrees(FPlatformTime::Seconds());
            if (Pitch > 0.0)
                InOutPOV.Rotation.Pitch = FRotator::NormalizeAxis(InOutPOV.Rotation.Pitch + Pitch);
            else
                State.Reset();
        }
        return false;
    }

private:
    FRegimentArtilleryKickState State;
};
