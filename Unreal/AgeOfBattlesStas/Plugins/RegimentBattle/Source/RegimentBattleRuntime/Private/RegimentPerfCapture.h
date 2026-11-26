#pragma once

#include "Containers/Array.h"
#include "Containers/Set.h"
#include "Containers/UnrealString.h"
#include "Misc/Optional.h"
#include "Templates/Function.h"
#include "battle/Simulation.h"
#include <span>

struct FRegimentPresentationCounts
{
    int32 Units = 0;
    int32 Alive = 0;
    int32 Rich = 0;
    int32 Proxy = 0;
    int32 Culled = 0;
};

struct FRegimentPresentationProfile
{
    double VisibilityMs = 0.0;
    double InstanceSubmitMs = 0.0;
    double ProjectilesMs = 0.0;
    double HudMs = 0.0;
    int32 MassUpdates = 0;
    int32 MassUnchanged = 0;
};

class FRegimentPerfCapture
{
public:
    FRegimentPerfCapture(int32 InStepLimit, FString OutputPath, bool bInFullDetail, bool bInScripted = false);
    bool IsMeasuring() const { return !bReported; }
    bool IsReady() const { return Steps >= StepLimit; }
    bool IsScripted() const { return bScripted; }
    static bool IsOrbitTick(uint64 Tick) { return Tick >= 120 && Tick < 180; }
    static double OrbitYaw(uint64 Tick) { return IsOrbitTick(Tick) ? double(Tick - 120) * 18.0 : 0.0; }
    void ObserveCameraMotion(uint64 Tick);
    void ObserveCombat(uint64 Tick, std::span<const battle::BattleEvent> Events);
    void RecordStep(bool bPossessed, double Milliseconds, const battle::StepProfile& Profile, uint64 Tick = 0);
    void RecordFrame(bool bPossessed, double Milliseconds, FRegimentPresentationCounts Counts, uint64 Tick = 0,
        const FRegimentPresentationProfile& Profile = {});
    void RecordGameFrame(bool bPossessed, double TimeSeconds, uint64 Tick = 0);
    FString Finish(uint64 Tick, TOptional<uint64> StateHash = {});

private:
    struct FSample
    {
        uint64 Tick;
        double Milliseconds;
    };

    struct FSamples
    {
        TArray<FSample> StepMs;
        battle::StepProfile PhaseTotals{};
        TArray<FSample> PresentMs;
        TArray<FSample> GameFrameMs;
        FRegimentPresentationCounts Last;
        FRegimentPresentationProfile PresentationTotals;
    };

    static FString WindowReport(const TCHAR* Name, const TCHAR* Mode, const FSamples& Samples,
        TFunctionRef<bool(uint64)> Inside);

    FSamples Modes[2];
    TSet<uint64> ArtilleryTicks;
    TSet<uint64> OrbitCameraTicks;
    TOptional<uint64> FirstMelee;
    TOptional<uint64> FirstArtillery;
    FString Path;
    int32 StepLimit;
    int32 Steps = 0;
    bool bReported = false;
    bool bFullDetail = false;
    bool bScripted = false;
    double LastGameFrameSeconds = 0.0;
};
