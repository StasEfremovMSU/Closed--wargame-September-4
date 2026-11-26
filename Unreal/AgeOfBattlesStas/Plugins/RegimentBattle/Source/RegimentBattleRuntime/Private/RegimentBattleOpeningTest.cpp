#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentBattleOpening.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentBattleOpeningTest, "RegimentBattle.Opening",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentBattleOpeningTest::RunTest(const FString& Parameters)
{
    battle::Scenario Scenario;
    for (uint64 Side = 0; Side < 2; ++Side)
    {
        battle::RegimentSpec Regiment;
        Regiment.id = { Side + 1 };
        Regiment.team = static_cast<int32>(Side);
        Regiment.pose = { { Side ? 17.f : -17.f, 0, 0 }, Side ? battle::Pi : 0.f };
        Regiment.formation = { 10, 1.1f, 0.25f };
        Scenario.regiments.push_back(Regiment);
        for (uint64 Index = 0; Index < 50; ++Index)
        {
            battle::UnitSpec Unit;
            Unit.id = { Side * 50 + Index + 1 };
            Unit.regiment = Regiment.id;
            Unit.pose = Regiment.pose;
            Unit.speed = 4.f;
            Scenario.units.push_back(Unit);
        }
    }
    auto Result = battle::CreateWorld(Scenario, 42);
    if (!TestTrue(TEXT("The formation fixture loads"), bool(Result)))
        return false;
    auto& World = Result.value;
    const auto Before = World.units;
    FRegimentBattleOpening Opening(World);
    TestTrue(TEXT("Opening starts active"), Opening.IsActive());

    for (uint32 Tick = 1; Tick <= FRegimentBattleOpening::DurationTicks; ++Tick)
    {
        const auto Previous = World.units;
        Opening.Advance(World);
        TestEqual(TEXT("Opening leaves BattleCore time at zero"), World.tick, uint64(0));
        bool bSpeedValid = true;
        bool bClear = true;
        for (size_t I = 0; I < World.units.size(); ++I)
        {
            const auto Delta = World.units[I].pose.position - Previous[I].pose.position;
            bSpeedValid &= battle::Length(Delta) <= 0.201f;
            for (size_t J = 0; J < I; ++J)
            {
                const auto BeforeDelta = Previous[I].pose.position - Previous[J].pose.position;
                const auto RelativeMovement = Delta - (World.units[J].pose.position - Previous[J].pose.position);
                const float SpeedSquared = battle::Dot(RelativeMovement, RelativeMovement);
                const float Closest = SpeedSquared > 0.f ? FMath::Clamp(
                    -battle::Dot(BeforeDelta, RelativeMovement) / SpeedSquared, 0.f, 1.f) : 0.f;
                bClear &= battle::Length(BeforeDelta + RelativeMovement * Closest) >= 1.09f;
            }
        }
        TestTrue(TEXT("No overlapping paths between fixed steps"), bClear);
        TestTrue(TEXT("Unit moves at no more than 4 m/s"), bSpeedValid);

        if (Tick == FRegimentBattleOpening::ColumnTick)
        {
            for (size_t Side = 0; Side < 2; ++Side)
            {
                const float Origin = World.regiments[Side].pose.position.x;
                float MaxWidth = 0.f;
                float MaxDepth = 0.f;
                for (size_t I = Side * 50; I < (Side + 1) * 50; ++I)
                {
                    MaxWidth = FMath::Max(MaxWidth, FMath::Abs(World.units[I].pose.position.y));
                    MaxDepth = FMath::Max(MaxDepth, FMath::Abs(World.units[I].pose.position.x - Origin));
                }
                TestTrue(TEXT("Both regiments form the five-wide column"), FMath::IsNearlyEqual(MaxWidth, 2.2f, .001f));
                TestTrue(TEXT("Both regiments have ten ranks in the column"), FMath::IsNearlyEqual(MaxDepth, 4.95f, .001f));
            }
        }
    }
    TestFalse(TEXT("The opening releases normal simulation"), Opening.IsActive());
    for (size_t I = 0; I < World.units.size(); ++I)
        TestTrue(TEXT("Every fighter returns to the original battle slot and facing"), World.units[I].pose == Before[I].pose);
    return true;
}
#endif
