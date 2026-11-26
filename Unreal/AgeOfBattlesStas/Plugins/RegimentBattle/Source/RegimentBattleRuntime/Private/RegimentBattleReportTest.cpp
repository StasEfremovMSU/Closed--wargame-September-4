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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentBattleCombatMomentsTest,
    "RegimentBattle.Results.Debrief.CombatMoments",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegimentBattleCombatMomentsTest::RunTest(const FString&)
{
    battle::World World;
    World.regiments.resize(2);
    World.regiments[0].id = {1};
    World.regiments[0].name = "Forward battalion";
    World.regiments[0].units = {{1}};
    World.regiments[1].id = {87};
    World.regiments[1].name = "Rear battalion";
    World.regiments[1].team = 1;
    World.regiments[1].units = {{2}};
    World.units.resize(2);
    World.units[0].id = {1};
    World.units[0].regiment = {1};
    World.units[1].id = {2};
    World.units[1].regiment = {87};
    FRegimentBattleReportAccumulator Accumulator(World);

    World.tick = 40;
    World.events = {{{1}, 40, battle::EventKind::Hit, {1}, {2}, {87}, 10.f, "melee attack"}};
    Accumulator.Observe(World);
    World.tick = 80;
    World.regiments[1].panic = true;
    World.events = {{{2}, 80, battle::EventKind::OrderChanged, {}, {}, {87}, 0.f,
        "panic: attacked from the rear"}};
    Accumulator.Observe(World);
    Accumulator.Observe(World);
    World.tick = 163;
    World.regiments[1].units.clear();
    World.units[1].alive = false;
    World.events = {{{3}, 163, battle::EventKind::OrderChanged, {}, {}, {87}, 0.f,
        "rout: fled off the battlefield"}};
    World.outcome = battle::BattleOutcome::Victory;
    World.winner_team = 0;

    const auto Report = Accumulator.Finish(World);
    if (!TestTrue(TEXT("A finished battle has a report"), Report.IsSet()) ||
        !TestEqual(TEXT("The debrief shows two turning points and the outcome"), Report->Moments.Num(), 3))
        return false;
    TestTrue(TEXT("An early panic matters more than routine damage"),
        Report->Moments[0].Kind == ERegimentBattleMomentKind::Panic);
    TestEqual(TEXT("The panic retains its original simulation tick"), Report->Moments[0].Tick, battle::Tick(80));
    TestTrue(TEXT("The panic identifies the affected battalion"),
        Report->Moments[0].Description.ToString().Contains(TEXT("Rear battalion")));
    TestTrue(TEXT("The final rout survives the finishing tick"),
        Report->Moments[1].Kind == ERegimentBattleMomentKind::Rout);
    TestEqual(TEXT("The rout and finish retain the same tick"), Report->Moments[1].Tick, battle::Tick(163));
    TestTrue(TEXT("The outcome follows events at the same tick"),
        Report->Moments[2].Kind == ERegimentBattleMomentKind::BattleEnded);
    TestEqual(TEXT("The final entry is timed"), Report->Moments[2].Tick, battle::Tick(163));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentBattleMissionMomentsTest,
    "RegimentBattle.Results.Debrief.MissionMoments",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegimentBattleMissionMomentsTest::RunTest(const FString&)
{
    battle::World World;
    FRegimentBattleReportAccumulator Accumulator(World);
    World.tick = 240;
    World.outcome = battle::BattleOutcome::Victory;
    World.winner_team = 0;
    const auto Report = Accumulator.Finish(World, {battle::Tick(142), battle::Tick(38)});
    if (!TestTrue(TEXT("A finished mission has a report"), Report.IsSet()) ||
        !TestEqual(TEXT("Both observed objective milestones appear"), Report->Moments.Num(), 3))
        return false;
    TestTrue(TEXT("Mission milestones use chronological order"),
        Report->Moments[0].Kind == ERegimentBattleMomentKind::ReserveContact &&
        Report->Moments[1].Kind == ERegimentBattleMomentKind::CrestControl);
    TestEqual(TEXT("Reserve contact retains its actual tick"), Report->Moments[0].Tick, battle::Tick(38));
    TestEqual(TEXT("Crest control retains its actual tick"), Report->Moments[1].Tick, battle::Tick(142));

    FRegimentBattleReportAccumulator SparseBattle(battle::World{});
    const auto SparseReport = SparseBattle.Finish(World);
    return TestTrue(TEXT("An uneventful battle never invents turning points"),
        SparseReport && SparseReport->Moments.Num() == 1 &&
        SparseReport->Moments[0].Kind == ERegimentBattleMomentKind::BattleEnded) && !HasAnyErrors();
}

#endif
