#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentOfficerInput.h"

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
#endif
