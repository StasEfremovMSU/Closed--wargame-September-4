#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentPerfCapture.h"
#include "battle/Combat.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentPerfWindowsTest, "RegimentBattle.Presentation.PerformanceWindows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentPerfWindowsTest::RunTest(const FString& Parameters)
{
    FRegimentPerfCapture Capture(145, {}, false, true);
    Capture.ObserveCameraMotion(119);
    Capture.ObserveCameraMotion(123);
    Capture.ObserveCameraMotion(123);
    Capture.ObserveCameraMotion(126);
    Capture.ObserveCameraMotion(180);
    for (uint64 Tick = 1; Tick <= 145; ++Tick)
    {
        battle::BattleEvent Event;
        if (Tick == 45)
        {
            Event.kind = battle::EventKind::AttackLaunched;
            Event.reason = battle::FieldGunRoundShotReason;
        }
        else if (Tick == 55)
        {
            Event.kind = battle::EventKind::Hit;
            Event.reason = battle::FieldGunRoundShotReason;
            Event.projectile = true;
        }
        else if (Tick == 63)
        {
            Event.kind = battle::EventKind::Hit;
            Event.projectile = true;
        }
        else if (Tick == 80)
            Event.kind = battle::EventKind::Hit;
        else
            Event.kind = battle::EventKind::DecisionTrace;
        Capture.ObserveCombat(Tick, {&Event, 1});
        Capture.RecordStep(false, Tick == 45 ? 91.0 : Tick == 80 ? 41.0 : 1.0, {}, Tick);
        Capture.RecordFrame(false, 1.0, {5000, 4999, 0, 4999, 0}, Tick);
        Capture.RecordGameFrame(false, double(Tick) * .01, Tick);
    }

    const FString Report = Capture.Finish(145, uint64(0x1234));
    TestTrue(TEXT("The camera-only run publishes a deterministic final fingerprint"),
        Report.Contains(TEXT("state_hash=0000000000001234")));
    TestTrue(TEXT("The first real hand-to-hand hit ignores earlier ranged hits"),
        Report.Contains(TEXT("first_melee_tick=80")));
    TestTrue(TEXT("Artillery timings capture launch and impact with their following ticks"),
        Report.Contains(TEXT("name=artillery mode=overview steps=6")) &&
        Report.Contains(TEXT("step_p99_ms=91.000")));
    TestTrue(TEXT("The orbit window records only its elapsed simulation ticks"),
        Report.Contains(TEXT("name=quick_orbit mode=overview steps=26")));
    TestTrue(TEXT("The report counts unique observed camera motions in the orbit window"),
        Report.Contains(TEXT("orbit_camera_ticks=2")));
    return !HasAnyErrors();
}
#endif
