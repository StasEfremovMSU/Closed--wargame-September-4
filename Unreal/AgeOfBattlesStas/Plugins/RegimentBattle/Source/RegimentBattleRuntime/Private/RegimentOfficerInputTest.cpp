#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentBattlePlayback.h"
#include "RegimentOfficerInput.h"
#include "RegimentOfficerView.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentOfficerInputTest, "RegimentBattle.Presentation.OfficerInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentOfficerInputTest::RunTest(const FString& Parameters)
{
    FRegimentOfficerInput Input;
    TestFalse(TEXT("Officer starts with a clear viewport"), Input.bCommandsOpen);
    Input.ToggleCommands(false);
    TestTrue(TEXT("Tab opens the commands drawer"), Input.bCommandsOpen);
    Input.ToggleCommands(false);
    TestFalse(TEXT("Tab closes the same drawer"), Input.bCommandsOpen);
    Input.ToggleCommands(true);
    TestFalse(TEXT("Aiming a maneuver keeps the drawer closed"), Input.bCommandsOpen);

    using EAttack = FRegimentOfficerInput::EAttack;
    TestTrue(TEXT("No input means no attack"), Input.ConsumeActions(true).Attack == EAttack::None);
    Input.RememberActions(true, false, true);
    Input.RememberActions(false, false, false);
    const auto Pressed = Input.ConsumeActions(true);
    TestTrue(TEXT("A tap that ends between simulation steps queues strike and jump"),
        Pressed.Attack == EAttack::Strike && Pressed.bJump);
    const auto Clear = Input.ConsumeActions(false);
    TestTrue(TEXT("Opening an order panel still clears the latched core target"),
        Clear.Attack == EAttack::Clear && !Clear.bJump && !Clear.bParry);
    TestTrue(TEXT("An idle subsequent step does not repeat the swing"),
        Input.ConsumeActions(true).Attack == EAttack::None);

    Input.RememberActions(true, true, true);
    Input.ToggleCommands(false);
    Input.ToggleCommands(false);
    const auto AfterCommands = Input.ConsumeActions(true);
    TestTrue(TEXT("Returning from the drawer discards earlier action edges"),
        AfterCommands.Attack == EAttack::None && !AfterCommands.bJump && !AfterCommands.bParry);
    Input.RememberActions(true, true, true);
    const auto AfterPlot = Input.ConsumeActions(false);
    TestTrue(TEXT("Plotting drains all unprocessed direct action edges"),
        AfterPlot.Attack == EAttack::None && !AfterPlot.bJump && !AfterPlot.bParry);
    Input.RememberActions(true, true, false);
    const auto Guard = Input.ConsumeActions(true);
    TestTrue(TEXT("The defensive press takes priority over a simultaneous swing"),
        Guard.Attack == EAttack::None && Guard.bParry);
    TestFalse(TEXT("An idle subsequent step does not repeat the parry"), Input.ConsumeActions(true).bParry);
    TestTrue(TEXT("Shift uses the existing maximum speed"), FRegimentOfficerInput::Speed(4.f, true) == 4.f);
    TestTrue(TEXT("Walking is slower than running"), FRegimentOfficerInput::Speed(4.f, false) < 4.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentOfficerViewMotionTest, "RegimentBattle.Presentation.OfficerViewMotion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentOfficerViewMotionTest::RunTest(const FString&)
{
    FRegimentBattlePlayback Playback;
    Playback.AddTime(.060);
    TestTrue(TEXT("A simulation step advances the pose endpoints"), Playback.TakeStep());
    TestEqual(TEXT("The residual time supplies a fractional pose"), Playback.RenderAlpha(), .2, 1e-6);
    Playback.SetPaused(true);
    Playback.AddTime(.040);
    TestEqual(TEXT("Pausing freezes the rendered body at the same point"), Playback.RenderAlpha(), .2, 1e-6);
    Playback.SetPaused(false);
    Playback.AddTime(.10, 2);
    TestEqual(TEXT("A dropped simulation step cannot extrapolate the rider"), Playback.RenderAlpha(), 1.0, 1e-6);

    constexpr double Windup = .10;
    TestEqual(TEXT("A new stroke begins at the resting grip"), RegimentOfficerView::BladeReach(0.0, Windup), 0.0, 1e-6);
    TestTrue(TEXT("The grip draws back before swinging through"), RegimentOfficerView::BladeReach(.04, Windup) < 0.0);
    TestEqual(TEXT("The blade reaches the target at the impact tick"), RegimentOfficerView::BladeReach(.10, Windup), 1.0, 1e-6);
    TestTrue(TEXT("The hand recovers after it contacts the target"), RegimentOfficerView::BladeReach(.20, Windup) < 1.0);
    TestEqual(TEXT("The recovered blade returns to rest"), RegimentOfficerView::BladeReach(.40, Windup), 0.0, 1e-6);
    return true;
}
#endif
