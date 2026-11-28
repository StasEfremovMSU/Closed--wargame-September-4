#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentArtilleryCameraKick.h"
#include "RegimentFigureLod.h"
#include "RegimentOfficerView.h"
#include "RegimentTacticalCamera.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentOfficerViewTest, "RegimentBattle.Presentation.OfficerFirstPerson",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentOfficerViewTest::RunTest(const FString&)
{
    const FVector Footing(240, 460, 31);
    const auto FootView = RegimentOfficerView::Eyes(Footing, false);
    const auto MountedView = RegimentOfficerView::Eyes(Footing, true);
    TestEqual(TEXT("The officer sees from their own position"), FootView, Footing + FVector(0, 0, 159));
    TestEqual(TEXT("Mounted officers see from the saddle"), MountedView, Footing + FVector(0, 0, 219));
    TestEqual(TEXT("Jump height is reflected without a chase delay"),
        RegimentOfficerView::Eyes(Footing + FVector(0, 0, 43), false) - FootView, FVector(0, 0, 43));
    TestEqual(TEXT("First person can look upward"), RegimentOfficerView::Pitch(65, true), 65.0);
    TestEqual(TEXT("Shoulder camera retains its safe upper angle"), RegimentOfficerView::Pitch(65, false), 24.0);
    TestTrue(TEXT("First person keeps greater peripheral awareness"),
        RegimentOfficerView::FieldOfView(true) > RegimentOfficerView::FieldOfView(false));
    return !HasAnyErrors();
}

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentCameraFollowTest, "RegimentBattle.Presentation.RegimentFollow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentCameraFollowTest::RunTest(const FString&)
{
    const FVector Origin(0, 0, 50);
    const battle::MarchField Field{540, 420};
    FRegimentFollowCamera Follow;
    Follow.Frame(78.0, 500);
    const FVector FirstTarget(12500, -7600, 350);
    FRegimentView View{Origin + FVector(-5500, 450, 3800), FRotator(-38, 20, 0)};
    const auto First = Follow.Advance(View, FirstTarget, 1.f / 60.f, Origin, Field, {});
    TestTrue(TEXT("A roster selection moves the camera into frame without teleporting"),
        FVector::Dist(First.Location, View.Location) < 2500.f && !First.Location.Equals(View.Location));
    const auto Track = [&](FVector Target)
    {
        for (int32 Frame = 0; Frame < 90; ++Frame)
            View = Follow.Advance(View, Target, 1.f / 60.f, Origin, Field, {});
    };
    Track(FirstTarget);
    TestTrue(TEXT("The regiment stays in the middle of the viewport"),
        FVector::DotProduct(View.Rotation.Vector(), (FirstTarget - View.Location).GetSafeNormal()) > .999);
    const auto Before = View.Location;
    const FVector SecondTarget = FirstTarget + FVector(500, 250, 30);
    Track(SecondTarget);
    TestTrue(TEXT("The camera continues tracking a moving regiment"),
        (View.Location - Before).Equals(SecondTarget - FirstTarget, 5.f));
    const double BeforeZoom = FVector::Dist(View.Location, SecondTarget);
    Follow.Zoom(2.f);
    Track(SecondTarget);
    TestTrue(TEXT("Zoom keeps following the same subject"), FVector::Dist(View.Location, SecondTarget) < BeforeZoom &&
        FVector::DotProduct(View.Rotation.Vector(), (SecondTarget - View.Location).GetSafeNormal()) > .999);
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
