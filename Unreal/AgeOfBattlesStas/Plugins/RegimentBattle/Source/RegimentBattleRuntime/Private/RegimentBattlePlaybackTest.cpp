#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentBattlePlayback.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentBattlePlaybackTest, "RegimentBattle.Playback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentBattlePlaybackTest::RunTest(const FString& Parameters)
{
    FRegimentBattlePlayback Playback;
    Playback.AddTime(0.025);
    TestFalse(TEXT("Half of a tick waits for the next frame"), Playback.TakeStep());
    Playback.SetPaused(true);
    Playback.AddTime(5.0);
    TestFalse(TEXT("Paused time does not advance the battle"), Playback.TakeStep());
    Playback.SetPaused(false);
    Playback.AddTime(0.025);
    TestTrue(TEXT("Resume preserves the partial tick"), Playback.TakeStep());
    TestFalse(TEXT("Resume has no backlog from pause"), Playback.TakeStep());

    Playback.SetSpeed(ERegimentBattleSpeed::Double);
    Playback.AddTime(0.05);
    TestTrue(TEXT("Double speed advances one tick"), Playback.TakeStep());
    TestTrue(TEXT("Double speed advances a second tick"), Playback.TakeStep());
    TestFalse(TEXT("Double speed does not advance a third tick"), Playback.TakeStep());
    Playback.SetSpeed(ERegimentBattleSpeed::Half);
    Playback.AddTime(0.05);
    TestFalse(TEXT("Half speed waits for another frame"), Playback.TakeStep());
    Playback.AddTime(0.05);
    TestTrue(TEXT("Half speed advances once after two frames"), Playback.TakeStep());

    Playback.AddTime(0.05);
    Playback.SetPaused(true);
    Playback.ResetClock();
    TestTrue(TEXT("Restart preserves pause"), Playback.IsPaused());
    TestTrue(TEXT("Restart preserves the selected speed"), Playback.GetSpeed() == ERegimentBattleSpeed::Half);
    Playback.SetPaused(false);
    Playback.AddTime(0.05);
    TestFalse(TEXT("Restart clears unfinished time"), Playback.TakeStep());

    Playback.AddTime(1.0, 2);
    TestTrue(TEXT("A slow frame can leave one step to catch up"), Playback.TakeStep());
    TestTrue(TEXT("The allowed second step is kept"), Playback.TakeStep());
    TestFalse(TEXT("Elapsed time beyond the large battle backlog is discarded"), Playback.TakeStep());
    Playback.SetSpeed(ERegimentBattleSpeed::Double);
    Playback.SetOfficerSelection(true);
    Playback.AddTime(60);
    TestTrue(TEXT("Officer selection pauses the battle"), Playback.IsPaused());
    TestFalse(TEXT("Selecting an officer cannot spend a tick"), Playback.TakeStep());
    Playback.SetOfficerSelection(false);
    TestFalse(TEXT("Closing selection resumes a running battle"), Playback.IsPaused());
    TestFalse(TEXT("Selection time produces no catch-up"), Playback.TakeStep());
    TestTrue(TEXT("Selection preserves playback speed"), Playback.GetSpeed() == ERegimentBattleSpeed::Double);
    Playback.SetPaused(true);
    Playback.SetOfficerSelection(true);
    Playback.SetOfficerSelection(false);
    TestTrue(TEXT("Closing selection keeps an existing player pause"), Playback.IsPaused());
    return true;
}
#endif
