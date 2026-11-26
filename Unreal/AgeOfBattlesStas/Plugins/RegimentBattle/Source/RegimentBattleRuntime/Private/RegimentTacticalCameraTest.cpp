#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentArtilleryCameraKick.h"
#include "RegimentFigureLod.h"
#include "RegimentTacticalCamera.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentCameraFlightTest, "RegimentBattle.Presentation.MapFlyover",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentCameraFlightTest::RunTest(const FString& Parameters)
{
    const FVector Origin(200, 300, 40);
    const FVector Start = Origin + FVector(0, -2600, 1700);
    const FRotator StartRotation(-34, 94, 0);
    const battle::TerrainHill Hill;
    const double SummitZ = Origin.Z + double(Hill.height_m) * 100.0;
    for (const battle::MarchField Field : {battle::MarchField{120, 76}, battle::MarchField{560, 360}})
    {
        FRegimentMapFlight Flight;
        Flight.Start(Start, StartRotation, Origin, Field);
        TestTrue(TEXT("Starting enables the flyover"), Flight.IsActive());
        const auto Initial = Flight.Sample(0.0);
        const auto Entry = Flight.Sample(Flight.IntroSeconds);
        const auto Opposite = Flight.Sample(Flight.IntroSeconds + Flight.OrbitSeconds * .5);
        const auto End = Flight.Sample(Flight.IntroSeconds + Flight.OrbitSeconds);
        TestTrue(TEXT("The flyover starts from the current view"), Initial.Location.Equals(Start) &&
            Initial.Rotation.Equals(StartRotation));
        TestTrue(TEXT("The flyover reaches the far side of the whole battlefield"),
            Entry.Location.Y < Origin.Y - Field.HalfHeight() * 100 &&
            Opposite.Location.Y > Origin.Y + Field.HalfHeight() * 100);
        TestTrue(TEXT("The orbit closes on its starting view"), End.Location.Equals(Entry.Location, 0.01));
        TestTrue(TEXT("The height gives clearance over the summit"), Entry.Location.Z > SummitZ);
        const auto FinalView = Flight.Advance(Flight.IntroSeconds + Flight.OrbitSeconds + 1.0);
        TestFalse(TEXT("The flyover gives back control at the end"), Flight.IsActive());
        TestTrue(TEXT("The flight does not overshoot its end"), FinalView.Location.Equals(End.Location));
    }

    const FVector AtCrest(Origin.X, Origin.Y, Origin.Z + 20);
    TestEqual(TEXT("A free camera cannot descend into the hill"), RegimentFlightCameraLocation(
        AtCrest, Origin, {120, 76}, Hill).Z, SummitZ + 150.0);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentCameraZoomTest, "RegimentBattle.Presentation.CursorZoom",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentCameraZoomTest::RunTest(const FString& Parameters)
{
    const FVector Origin(200, 300, 40);
    const FVector Anchor = Origin + FVector(550, 180, 0);
    const FVector Start = Origin + FVector(-700, -1800, 1550);
    const battle::MarchField Field{120, 76};
    const auto Near = RegimentCameraZoomTarget(Start, Anchor, 2.0, Origin, Field, {});
    TestTrue(TEXT("Scrolling in moves the camera nearer to the point under the cursor"),
        FVector::Dist(Near, Anchor) < FVector::Dist(Start, Anchor));
    TestTrue(TEXT("The selected point remains on the same viewing ray"),
        (Near - Anchor).GetSafeNormal().Equals((Start - Anchor).GetSafeNormal()));
    TestTrue(TEXT("Opposite scroll steps restore distance before reaching a limit"),
        RegimentCameraZoomTarget(Near, Anchor, -2.0, Origin, Field, {}).Equals(Start));

    const battle::TerrainHill Hill;
    const FVector Summit = Origin + FVector(0, 0, double(Hill.height_m) * 100.0);
    const FVector OverSummit = Summit + FVector(0, 0, 1200);
    const auto NearSummit = RegimentCameraZoomTarget(OverSummit, Summit, 60.0, Origin, Field, Hill);
    TestTrue(TEXT("A long scroll stops just above the hill's minimum clearance"),
        FMath::IsNearlyEqual(NearSummit.Z, Summit.Z + 150.0, 1.0));
    TestTrue(TEXT("Zooming out also respects the existing height limit"),
        FMath::IsNearlyEqual(RegimentCameraZoomTarget(Start, Anchor, -60.0, Origin, Field, {}).Z,
            Origin.Z + 13000.0, 2.0));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentOfficerLodRangeTest, "RegimentBattle.Presentation.OfficerLodRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentOfficerLodRangeTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Officer and observer modes use a 73.5 m radius at large battle scale"),
        RegimentRichFigureDistance(10000), 7350.);
    TestEqual(TEXT("Officer and observer modes use a 192 m radius for small battles"),
        RegimentRichFigureDistance(120), 19200.);
    TestEqual(TEXT("The range increase keeps the existing rich mesh budget"), RichFigureLimit, 160);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentArtilleryCameraKickTest, "RegimentBattle.Presentation.ArtilleryCameraKick",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentArtilleryCameraKickTest::RunTest(const FString& Parameters)
{
    constexpr double LaunchTime = 10.0;
    FRegimentArtilleryKickState Kick;
    Kick.Trigger(2600.0, LaunchTime);
    TestEqual(TEXT("A distant gun leaves the camera steady"), Kick.PitchDegrees(LaunchTime), 0.0);

    Kick.Trigger(1400.0, LaunchTime);
    const double MediumPitch = Kick.PitchDegrees(LaunchTime);
    Kick.Trigger(300.0, LaunchTime);
    const double ClosePitch = Kick.PitchDegrees(LaunchTime);
    TestTrue(TEXT("A nearer shot is stronger and the pitch still stays bounded"),
        MediumPitch > 0.0 && ClosePitch > MediumPitch && ClosePitch <= 1.2);
    Kick.Trigger(300.0, LaunchTime + .05);
    const double SettlingPitch = Kick.PitchDegrees(LaunchTime + .1);
    TestTrue(TEXT("Repeated nearby shots yield a single decaying kick"),
        SettlingPitch > 0.0 && SettlingPitch < ClosePitch);
    TestEqual(TEXT("Sampling the same time for two camera views gives the same offset"),
        Kick.PitchDegrees(LaunchTime + .1), SettlingPitch);
    TestEqual(TEXT("The view fully settles after the impulse"),
        Kick.PitchDegrees(LaunchTime + .26), 0.0);
    Kick.Trigger(300.0, LaunchTime + .3);
    TestTrue(TEXT("A later shot can start a new kick"), Kick.PitchDegrees(LaunchTime + .3) > 0.0);
    Kick.Reset();
    TestEqual(TEXT("Pause or mission change clears an active kick immediately"),
        Kick.PitchDegrees(LaunchTime + .3), 0.0);
    return !HasAnyErrors();
}
#endif
