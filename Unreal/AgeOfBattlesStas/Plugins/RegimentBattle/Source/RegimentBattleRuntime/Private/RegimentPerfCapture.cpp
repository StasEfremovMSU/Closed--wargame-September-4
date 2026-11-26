#include "RegimentPerfCapture.h"

#include "Math/UnrealMathUtility.h"
#include "Misc/FileHelper.h"
#include "battle/Combat.h"

namespace
{
double Percentile(const TArray<double>& Values, double Fraction)
{
    return Values.IsEmpty() ? 0.0 : Values[FMath::CeilToInt(Values.Num() * Fraction) - 1];
}

auto Times(const auto& Samples, TFunctionRef<bool(uint64)> Inside)
{
    TArray<double> Values;
    for (const auto& Sample : Samples)
        if (Inside(Sample.Tick))
            Values.Add(Sample.Milliseconds);
    Values.Sort();
    return Values;
}
}

FRegimentPerfCapture::FRegimentPerfCapture(
    int32 InStepLimit, FString OutputPath, bool bInFullDetail, bool bInScripted)
    : Path(MoveTemp(OutputPath)), StepLimit(InStepLimit), bFullDetail(bInFullDetail), bScripted(bInScripted)
{
    for (FSamples& Samples : Modes)
    {
        Samples.StepMs.Reserve(StepLimit);
        Samples.PresentMs.Reserve(StepLimit);
        Samples.GameFrameMs.Reserve(StepLimit);
    }
}

void FRegimentPerfCapture::ObserveCameraMotion(uint64 Tick)
{
    if (IsOrbitTick(Tick))
        OrbitCameraTicks.Add(Tick);
}

void FRegimentPerfCapture::ObserveCombat(uint64 Tick, std::span<const battle::BattleEvent> Events)
{
    if (!bScripted)
        return;
    for (const auto& Event : Events)
    {
        if (!FirstMelee && Event.kind == battle::EventKind::Hit && !Event.projectile)
            FirstMelee = Tick;
        if ((Event.kind == battle::EventKind::AttackLaunched || Event.kind == battle::EventKind::Hit) &&
            (Event.reason == battle::FieldGunRoundShotReason || Event.reason == battle::FieldGunCanisterReason))
        {
            if (!FirstArtillery)
                FirstArtillery = Tick;
            ArtilleryTicks.Add(Tick);
            ArtilleryTicks.Add(Tick + 1);
            ArtilleryTicks.Add(Tick + 2);
        }
    }
}

void FRegimentPerfCapture::RecordStep(
    bool bPossessed, double Milliseconds, const battle::StepProfile& Profile, uint64 Tick)
{
    auto& Samples = Modes[bPossessed];
    Samples.StepMs.Add({Tick, Milliseconds});
    Samples.PhaseTotals.orders_ms += Profile.orders_ms;
    Samples.PhaseTotals.movement_ms += Profile.movement_ms;
    Samples.PhaseTotals.targeting_ms += Profile.targeting_ms;
    Samples.PhaseTotals.commit_ms += Profile.commit_ms;
    ++Steps;
}

void FRegimentPerfCapture::RecordFrame(
    bool bPossessed, double Milliseconds, FRegimentPresentationCounts Counts, uint64 Tick,
    const FRegimentPresentationProfile& Profile)
{
    auto& Samples = Modes[bPossessed];
    Samples.PresentMs.Add({Tick, Milliseconds});
    Samples.Last = Counts;
    Samples.PresentationTotals.VisibilityMs += Profile.VisibilityMs;
    Samples.PresentationTotals.InstanceSubmitMs += Profile.InstanceSubmitMs;
    Samples.PresentationTotals.ProjectilesMs += Profile.ProjectilesMs;
    Samples.PresentationTotals.HudMs += Profile.HudMs;
    Samples.PresentationTotals.MassUpdates += Profile.MassUpdates;
    Samples.PresentationTotals.MassUnchanged += Profile.MassUnchanged;
}

void FRegimentPerfCapture::RecordGameFrame(bool bPossessed, double TimeSeconds, uint64 Tick)
{
    constexpr int32 MaxFrameSamplesPerMode = 100000;
    if (LastGameFrameSeconds > 0.0 && Modes[bPossessed].GameFrameMs.Num() < MaxFrameSamplesPerMode)
        Modes[bPossessed].GameFrameMs.Add({Tick, (TimeSeconds - LastGameFrameSeconds) * 1000.0});
    LastGameFrameSeconds = TimeSeconds;
}

FString FRegimentPerfCapture::WindowReport(
    const TCHAR* Name, const TCHAR* Mode, const FSamples& Samples, TFunctionRef<bool(uint64)> Inside)
{
    const auto Step = Times(Samples.StepMs, Inside);
    const auto Present = Times(Samples.PresentMs, Inside);
    const auto Frame = Times(Samples.GameFrameMs, Inside);
    return FString::Printf(
        TEXT("RegimentPerfWindow name=%s mode=%s steps=%d frames=%d present_frames=%d ")
        TEXT("step_p95_ms=%.3f step_p99_ms=%.3f present_p95_ms=%.3f present_p99_ms=%.3f ")
        TEXT("frame_p95_ms=%.3f frame_p99_ms=%.3f\n"),
        Name, Mode, Step.Num(), Frame.Num(), Present.Num(), Percentile(Step, .95), Percentile(Step, .99),
        Percentile(Present, .95), Percentile(Present, .99), Percentile(Frame, .95), Percentile(Frame, .99));
}

FString FRegimentPerfCapture::Finish(uint64 Tick, TOptional<uint64> StateHash)
{
    if (bReported)
        return {};
    bReported = true;

    const auto Always = [](uint64) { return true; };
    FString Summary;
    for (int32 Mode = 0; Mode < 2; ++Mode)
    {
        const FSamples& Samples = Modes[Mode];
        if (Samples.StepMs.IsEmpty() && Samples.PresentMs.IsEmpty())
            continue;

        const auto Step = Times(Samples.StepMs, Always);
        const auto Present = Times(Samples.PresentMs, Always);
        const auto Frame = Times(Samples.GameFrameMs, Always);
        const TCHAR* ModeName = Mode == 0 ? TEXT("overview") : TEXT("possessed");
        const FRegimentPresentationCounts& Counts = Samples.Last;
        const double StepCount = FMath::Max(1, Samples.StepMs.Num());
        Summary += FString::Printf(
            TEXT("RegimentPerf mode=%s lod=%s tick=%llu complete=%d steps=%d frames=%d present_frames=%d units=%d alive=%d rich=%d proxy=%d culled=%d ")
            TEXT("step_p50_ms=%.3f step_p95_ms=%.3f step_p99_ms=%.3f present_p50_ms=%.3f present_p95_ms=%.3f present_p99_ms=%.3f ")
            TEXT("frame_p50_ms=%.3f frame_p95_ms=%.3f frame_p99_ms=%.3f ")
            TEXT("orders_avg_ms=%.3f movement_avg_ms=%.3f targeting_avg_ms=%.3f commit_avg_ms=%.3f"),
            ModeName, bFullDetail ? TEXT("full") : TEXT("budgeted"), Tick, int32(IsReady()),
            Samples.StepMs.Num(), Samples.GameFrameMs.Num(), Samples.PresentMs.Num(),
            Counts.Units, Counts.Alive, Counts.Rich, Counts.Proxy, Counts.Culled,
            Percentile(Step, .50), Percentile(Step, .95), Percentile(Step, .99),
            Percentile(Present, .50), Percentile(Present, .95), Percentile(Present, .99),
            Percentile(Frame, .50), Percentile(Frame, .95), Percentile(Frame, .99),
            Samples.PhaseTotals.orders_ms / StepCount, Samples.PhaseTotals.movement_ms / StepCount,
            Samples.PhaseTotals.targeting_ms / StepCount, Samples.PhaseTotals.commit_ms / StepCount);
        const double PresentCount = FMath::Max(1, Samples.PresentMs.Num());
        const auto& Presentation = Samples.PresentationTotals;
        Summary += FString::Printf(
            TEXT(" visibility_avg_ms=%.3f instance_submit_avg_ms=%.3f visual_projectiles_avg_ms=%.3f hud_avg_ms=%.3f ")
            TEXT("mass_updates=%d mass_unchanged=%d"),
            Presentation.VisibilityMs / PresentCount, Presentation.InstanceSubmitMs / PresentCount,
            Presentation.ProjectilesMs / PresentCount, Presentation.HudMs / PresentCount,
            Presentation.MassUpdates, Presentation.MassUnchanged);
        if (bScripted)
        {
            Summary += FString::Printf(TEXT(" scripted=1 first_melee_tick=%llu first_artillery_tick=%llu orbit_camera_ticks=%d"),
                FirstMelee.Get(0), FirstArtillery.Get(0), OrbitCameraTicks.Num());
            if (StateHash)
                Summary += FString::Printf(TEXT(" state_hash=%016llx"), StateHash.GetValue());
        }
        Summary += TEXT("\n");
        if (bScripted)
        {
            Summary += WindowReport(TEXT("first_melee"), ModeName, Samples, [this](uint64 SampleTick)
            {
                return FirstMelee && SampleTick + 10 >= FirstMelee.GetValue() &&
                    SampleTick <= FirstMelee.GetValue() + 10;
            });
            Summary += WindowReport(TEXT("artillery"), ModeName, Samples,
                [this](uint64 SampleTick) { return ArtilleryTicks.Contains(SampleTick); });
            Summary += WindowReport(TEXT("quick_orbit"), ModeName, Samples, &IsOrbitTick);
        }
    }
    if (!Path.IsEmpty() && !Summary.IsEmpty() &&
        !FFileHelper::SaveStringToFile(Summary, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        Summary += TEXT("RegimentPerf output_write_failed\n");
    return Summary;
}
