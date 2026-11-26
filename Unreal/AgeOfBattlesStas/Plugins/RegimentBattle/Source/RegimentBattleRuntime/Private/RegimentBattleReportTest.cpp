#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "RegimentBattleReport.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

THIRD_PARTY_INCLUDES_START
#include "battle/World.h"
THIRD_PARTY_INCLUDES_END

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentBattleReportTest,
    "RegimentBattle.Results.ScoreboardAndLocalStatistics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegimentBattleReportTest::RunTest(const FString& Parameters)
{
    battle::World World;
    World.regiments.resize(2);
    World.regiments[0].id = {1};
    World.regiments[0].name = "First battalion";
    World.regiments[1].id = {87};
    World.regiments[1].name = "Second battalion";
    World.regiments[1].team = 1;
    World.units.resize(5);
    for (int32 Index = 0; Index < 5; ++Index)
        World.units[Index].id = {uint64(Index + 1)};
    World.units[0].regiment = World.units[1].regiment = {1};
    World.units[2].regiment = World.units[3].regiment = {87};

    FRegimentBattleReportAccumulator Observe(World);
    TestFalse(TEXT("Running battles have no report"), Observe.Finish(World).IsSet());
    World.tick = 240;
    World.units[0].alive = World.units[2].alive = World.units[4].alive = false;
    World.regiments[1].panic = true;
    World.events = {
        {{1}, 239, battle::EventKind::Died, {1}, {3}},
        {{2}, 239, battle::EventKind::Died, {1}, {3}},
        {{3}, 239, battle::EventKind::Died, {4}, {1}},
        {{4}, 239, battle::EventKind::Died, {2}, {5}},
        {{5}, 239, battle::EventKind::Hit, {2}, {4}}
    };
    Observe.Observe(World);
    Observe.Observe(World);
    World.outcome = battle::BattleOutcome::Victory;
    World.winner_team = 0;
    const auto Report = Observe.Finish(World);
    if (!TestTrue(TEXT("A finished battle produces a debrief"), Report.IsSet()))
        return false;
    TestFalse(TEXT("The same battle can finish only once"), Observe.Finish(World).IsSet());
    TestEqual(TEXT("Both battalions and the independent staff appear"), Report->Rows.Num(), 3);
    TestEqual(TEXT("Starting strength includes independent headquarters"), Report->Sides[0].Starting, 3);
    TestEqual(TEXT("Confirmed deaths reduce side strength"), Report->Sides[0].Fallen(), 2);
    TestEqual(TEXT("Both friendly fire and hostile fire cause losses"), Report->Sides[0].Living, 1);
    TestEqual(TEXT("Duplicate deaths and friendly fire do not award hostile eliminations"), Report->Sides[0].Eliminated, 1);
    TestEqual(TEXT("Both sides get credit on simultaneous fatal attacks"), Report->Sides[1].Eliminated, 1);
    TestEqual(TEXT("Panic only counts living figures"), Report->Sides[1].Panicked, 1);
    TestEqual(TEXT("Elapsed time comes from simulation ticks"), Report->ElapsedSeconds, uint64(12));

    FRegimentPlayerStats Stats;
    Stats.Record(*Report, 0);
    TestEqual(TEXT("One completed win"), Stats.Completed(), uint64(1));
    TestEqual(TEXT("Player victory count"), Stats.Victories, uint64(1));
    TestEqual(TEXT("Player-side statistics contain only the player's army"), Stats.Eliminated, uint64(1));
    TestEqual(TEXT("Player losses include the headquarters"), Stats.Fallen, uint64(2));
    const FString Path = FPaths::ProjectIntermediateDir() /
        (TEXT("RegimentBattleStats-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".txt"));
    TestTrue(TEXT("Saves local totals"), Stats.Save(Path));
    FRegimentPlayerStats Loaded;
    Loaded.Load(Path);
    TestEqual(TEXT("Loads the same completed count"), Loaded.Completed(), uint64(1));
    TestEqual(TEXT("Loads simulated seconds"), Loaded.ElapsedSeconds, uint64(12));
    TestEqual(TEXT("Loads losses"), Loaded.Fallen, uint64(2));
    auto& Files = IFileManager::Get();
    Files.Delete(*Path);
    Files.Delete(*(Path + TEXT(".bak")));
    return !HasAnyErrors();
}

#endif
