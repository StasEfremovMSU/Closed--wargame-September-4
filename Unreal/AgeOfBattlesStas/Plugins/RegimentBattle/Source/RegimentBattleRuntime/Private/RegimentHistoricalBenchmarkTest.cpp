#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "RegimentBattlePlayback.h"
#include "RegimentMissionProgress.h"
#include "RegimentMissionScenario.h"
#if WITH_EDITOR
#include "RegimentMissionAuthoring.h"
#endif

THIRD_PARTY_INCLUDES_START
#include "battle/Formation.h"
THIRD_PARTY_INCLUDES_END

namespace
{
const FGuid CannaeId(0xBD8E7423, 0xCDA541E0, 0xAD6DAF78, 0xD4C9F547);
const FGuid WaterlooId(0x7E801C0A, 0x1E9941BD, 0x983E0DFE, 0x4DC07B33);
const FGuid BorodinoId(0x36FE71C7, 0x5AFD565A, 0xBB6E7B4A, 0x60281750);

battle::Result<battle::World> MakeHistoricalWorld(const FMissionPreset& Preset)
{
    const auto Opening = MakeMissionOpening(Preset.Roster, Preset.Terrain, Preset.Field, Preset.FigureTarget);
    return battle::CreateWorld(BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart), 42);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentCannaeBenchmarkTest, "RegimentBattle.HistoricalBenchmark.Cannae",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegimentCannaeBenchmarkTest::RunTest(const FString&)
{
    FMissionPreset Preset;
    if (!TestTrue(TEXT("Cannae loads from the historical catalogue"),
        RegimentMissionPresets::LoadBuiltIn(CannaeId, Preset)))
        return false;

    auto Created = MakeHistoricalWorld(Preset);
    if (!TestTrue(TEXT("The actual deployment creates a world"), bool(Created)))
        return false;

    auto& World = Created.value;
    TestTrue(TEXT("The tactical world has a breakthrough marker"), World.scenario.objective.has_value());
    FRegimentMissionProgress Mission(Preset, World);
    const auto Deadline = battle::Tick(Preset.Objective.TimeLimitSeconds) * FRegimentBattlePlayback::StepsPerSecond;

    World.tick = Deadline - 1;
    Mission.Update(World);
    TestTrue(TEXT("The benchmark is still running before time expires"), World.outcome == battle::BattleOutcome::Running);

    World.tick = Deadline;
    Mission.Update(World);
    TestTrue(TEXT("Failing to reach the breakthrough area makes Carthage win at the deadline"),
        World.outcome == battle::BattleOutcome::Victory && World.winner_team == 1);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentAuthoringDiagnosticTest, "RegimentBattle.HistoricalBenchmark.Diagnostics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegimentAuthoringDiagnosticTest::RunTest(const FString&)
{
    FMissionPreset Preset;
    FText Error;
    Preset.Roster[8].Group = 6;
    TestFalse(TEXT("Rejects a group outside the command hierarchy"), RegimentMissionPresets::Validate(Preset, Error));
    TestTrue(TEXT("Diagnostic identifies the exact authored field"), Error.ToString().Contains(TEXT("regiments[8].group")));

    Preset.Roster[8].Group = 0;
    Preset.Roster[8].StartY = 100.f;
    TestFalse(TEXT("Rejects a coordinate beyond the current field"), RegimentMissionPresets::Validate(Preset, Error));
    TestTrue(TEXT("Coordinate diagnostic identifies its axis and slot"), Error.ToString().Contains(TEXT("regiments[8].start_y")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentCannaeFormationTest, "RegimentBattle.HistoricalBenchmark.CannaeFormation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegimentCannaeFormationTest::RunTest(const FString&)
{
    FMissionPreset Preset;
    if (!TestTrue(TEXT("Cannae loads"), RegimentMissionPresets::LoadBuiltIn(CannaeId, Preset)))
        return false;
    auto Created = MakeHistoricalWorld(Preset);
    if (!TestTrue(TEXT("Cannae deploys at intended scale"), bool(Created)))
        return false;

    auto& World = Created.value;
    auto* Lead = battle::FindRegiment(World, {1});
    auto* Second = battle::FindRegiment(World, {2});
    auto* Horse = battle::FindRegiment(World, {4});
    const auto* Center = battle::FindRegiment(World, {7});
    const auto* Wing = battle::FindRegiment(World, {9});
    if (!Lead || !Second || !Horse || !Center || !Wing || !World.scenario.objective)
    {
        AddError(TEXT("The scenario needs its five main formations and a tactical objective."));
        return false;
    }
    const auto Footprint = battle::Footprint(*Lead);
    TestTrue(TEXT("The Roman lead has a deep assault formation"), Footprint.depth_m > 1.5f * Footprint.width_m);
    TestTrue(TEXT("The Carthaginian center is forward of the infantry wing"),
        Center->pose.position.x + 22.f < Wing->pose.position.x);
    TestTrue(TEXT("The Roman cavalry occupies a separate flank lane"),
        FMath::Abs(Horse->pose.position.y - Lead->pose.position.y) > 50.f);
    TestEqual(TEXT("Keeps the historical-scale figure budget"), int32(World.units.size()), 5000);

    const auto Enter = [&World](battle::RegimentState& Regiment, float LateralOffset)
    {
        const auto Destination = World.scenario.objective->center + battle::Vec3{0.f, LateralOffset, 0.f};
        const auto Delta = Destination - Regiment.pose.position;
        Regiment.pose.position = Destination;
        for (const auto Id : Regiment.units)
            if (auto* Fighter = battle::FindUnit(World, Id))
                Fighter->pose.position = Fighter->pose.position + Delta;
    };

    FRegimentMissionProgress Progress(Preset, World);
    Enter(*Lead, -12.f);
    Enter(*Horse, 35.f);
    ++World.tick;
    Progress.Update(World);
    TestTrue(TEXT("One center regiment and flank cavalry do not complete the assigned breach"),
        World.outcome == battle::BattleOutcome::Running);
    Enter(*Second, 12.f);
    Second->panic = true;
    ++World.tick;
    Progress.Update(World);
    TestTrue(TEXT("A routed center formation cannot count toward the objective"),
        World.outcome == battle::BattleOutcome::Running);
    Second->panic = false;
    ++World.tick;
    Progress.Update(World);
    TestTrue(TEXT("Two formed Roman center regiments through the zone win"),
        World.outcome == battle::BattleOutcome::Victory && World.winner_team == 0);

    Preset.Objective.RequiredRegiments = 4;
    FText Error;
    TestFalse(TEXT("Reject impossible breakthrough count"), RegimentMissionPresets::Validate(Preset, Error));
    TestTrue(TEXT("The error names the breakthrough count field"),
        Error.ToString().Contains(TEXT("objective.required_regiments")));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentWaterlooBenchmarkTest, "RegimentBattle.HistoricalBenchmark.Waterloo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegimentWaterlooBenchmarkTest::RunTest(const FString&)
{
    FMissionPreset Preset;
    if (!TestTrue(TEXT("Waterloo loads"), RegimentMissionPresets::LoadBuiltIn(WaterlooId, Preset)))
        return false;
    auto Created = MakeHistoricalWorld(Preset);
    if (!TestTrue(TEXT("Waterloo deploys"), bool(Created)))
        return false;
    auto& World = Created.value;
    auto* Assault = battle::FindRegiment(World, {1});
    auto* Defender = battle::FindRegiment(World, {7});
    const auto* SupportingLine = battle::FindRegiment(World, {2});
    if (!Assault || !Defender || !SupportingLine || !World.scenario.hill)
    {
        AddError(TEXT("Expected the French column, firing line, British defenders and the ridge."));
        return false;
    }
    TestTrue(TEXT("French center attacks in column and its flank supports in line"),
        !battle::IsLinearFiringLine(*Assault) && battle::IsLinearFiringLine(*SupportingLine));
    TestTrue(TEXT("British front is a firing line"), battle::IsLinearFiringLine(*Defender));
    const auto Crest = battle::HillObjectiveCrest(*World.scenario.hill);
    if (!TestTrue(TEXT("Waterloo has a playable objective crest"), bool(Crest)))
        return false;

    const auto MoveTo = [&World](battle::RegimentState& Regiment, battle::Vec3 Point)
    {
        const auto Delta = Point - Regiment.pose.position;
        Regiment.pose.position = Point;
        for (const auto Id : Regiment.units)
            if (auto* Fighter = battle::FindUnit(World, Id))
                Fighter->pose.position = Fighter->pose.position + Delta;
    };
    const auto PlaceOnCrest = [&](battle::RegimentState& Regiment)
    {
        MoveTo(Regiment, {Crest->center.x, Crest->center.y, 0.f});
    };

    FRegimentMissionProgress Progress(Preset, World);
    PlaceOnCrest(*Assault);
    PlaceOnCrest(*Defender);
    const auto Hold = MissionHillHoldSeconds * FRegimentBattlePlayback::StepsPerSecond;
    for (int32 Sample = 0; Sample < Hold; ++Sample)
    {
        ++World.tick;
        Progress.Update(World);
    }
    TestTrue(TEXT("A French charge onto a still-defended ridge cannot win"),
        World.outcome == battle::BattleOutcome::Running);

    for (auto& Regiment : World.regiments)
        if (Regiment.team == 1)
            MoveTo(Regiment, {Preset.Field.HalfWidth() - 20.f, 0.f, 0.f});
    for (int32 Sample = 0; Sample < Hold; ++Sample)
    {
        ++World.tick;
        Progress.Update(World);
    }
    TestTrue(TEXT("Forcing the defenders off and holding the crest wins"),
        World.outcome == battle::BattleOutcome::Victory && World.winner_team == 0);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentBorodinoRedoubtTest, "RegimentBattle.HistoricalBenchmark.BorodinoRedoubt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegimentBorodinoRedoubtTest::RunTest(const FString&)
{
    FMissionPreset Preset;
    if (!TestTrue(TEXT("Borodino loads"), RegimentMissionPresets::LoadBuiltIn(BorodinoId, Preset)))
        return false;
    auto Created = MakeHistoricalWorld(Preset);
    if (!TestTrue(TEXT("Borodino deploys"), bool(Created)))
        return false;
    const auto& World = Created.value;
    const auto* Battery = battle::FindRegiment(World, {10});
    const auto Inside = std::ranges::find_if(World.scenario.obstacles, [](const auto& Area)
    {
        return Area.feature == battle::TerrainFeature::RedoubtInterior && Area.min_y < 0.f && Area.max_y > 0.f;
    });
    if (!TestTrue(TEXT("Raevsky's battery and the central redoubt exist"),
        Battery && Inside != World.scenario.obstacles.end()))
        return false;
    const auto InRedoubt = [&Inside](battle::Vec3 Point)
    {
        return Point.x > Inside->min_x && Point.x < Inside->max_x &&
            Point.y > Inside->min_y && Point.y < Inside->max_y;
    };
    int32 Guns = 0;
    for (const auto Id : Battery->units)
        if (const auto* Unit = battle::FindUnit(World, Id); Unit && Unit->platform == battle::BattlePlatform::FieldGun)
        {
            ++Guns;
            TestTrue(TEXT("Each cannon starts inside the battery earthworks"), InRedoubt(Unit->pose.position));
            TestTrue(TEXT("Gun positions are aligned along the frontage"),
                FMath::IsNearlyEqual(Unit->pose.position.x, Battery->pose.position.x, .002f));
        }
    TestEqual(TEXT("Raevsky's battery keeps its five field guns"), Guns, 5);
    return !HasAnyErrors();
}

#if WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentAuthoringPreviewTest, "RegimentBattle.HistoricalBenchmark.Preview",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegimentAuthoringPreviewTest::RunTest(const FString&)
{
    FMissionPreset Preset;
    Preset.Terrain = battle::MarchTerrain::StoneWalls;
    Preset.bDirectDeployment = true;
    for (auto& Entry : Preset.Roster)
        Entry.bEnabled = false;
    for (int32 Slot : {0, 1, MissionRegimentsPerTeam})
    {
        Preset.Roster[Slot].bEnabled = true;
        Preset.Roster[Slot].StartX = Slot == MissionRegimentsPerTeam ? 2.f : -18.f;
        Preset.Roster[Slot].StartY = -12.f;
    }
    FText Error;
    TestTrue(TEXT("Recoverable deployment remains a valid saved scenario"),
        RegimentMissionPresets::Validate(Preset, Error));
    const auto Preview = BuildMissionAuthoringPreview(Preset, Error);
    if (!TestTrue(TEXT("The authoring tool builds actual deployment geometry"), Preview.IsSet()))
        return false;
    TestEqual(TEXT("Preserves the center-to-edge field width"), Preview->Field.HalfWidth(), 60.f);
    TestTrue(TEXT("Includes the blocking stone wall geometry"),
        Preview->Terrain.ContainsByPredicate([](const auto& Area)
        { return Area.feature == battle::TerrainFeature::StoneWall && Area.min_x <= 0.f && Area.max_x >= 0.f; }));
    if (!TestEqual(TEXT("Only active regiments get spawn footprints"), Preview->Spawns.Num(), 3))
        return false;
    const auto& BlueLead = Preview->Spawns[0];
    const auto& BlueSecond = Preview->Spawns[1];
    const auto& Orange = Preview->Spawns[2];
    TestEqual(TEXT("The leading stable slot stays exactly where authored"), BlueLead.Actual.position.x, -18.f);
    TestTrue(TEXT("The second overlapping slot shows where the runtime relocated it"),
        BlueSecond.Actual.position != BlueSecond.Requested.position);
    TestTrue(TEXT("The orange slot next to the wall moves clear of its requested footprint"),
        Orange.Actual.position != Orange.Requested.position && Orange.Actual.position.x > 2.f);
    TestTrue(TEXT("A diagnostic identifies the two overlapping slot IDs"),
        Preview->Diagnostics.ContainsByPredicate([](const auto& Finding)
        { return Finding.Slot == 1 && Finding.OtherSlot == 0 && Finding.Kind == EMissionAuthoringIssue::Overlap; }));
    TestTrue(TEXT("A diagnostic identifies the walled-off requested footprint"),
        Preview->Diagnostics.ContainsByPredicate([](const auto& Finding)
        { return Finding.Slot == MissionRegimentsPerTeam && Finding.Kind == EMissionAuthoringIssue::Obstructed; }));
    const auto Created = MakeHistoricalWorld(Preset);
    if (!TestTrue(TEXT("The preview's scenario also starts in the runtime"), bool(Created)))
        return false;
    for (const auto& Spawn : Preview->Spawns)
    {
        const auto* Regiment = battle::FindRegiment(Created.value, {uint64(Spawn.Slot + 1)});
        if (!TestNotNull(TEXT("Every painted footprint refers to a runtime slot"), Regiment))
            return false;
        const auto RuntimeSize = battle::Footprint(*Regiment);
        TestTrue(TEXT("Footprint matches the actual formation"),
            Spawn.Size.width_m == RuntimeSize.width_m && Spawn.Size.depth_m == RuntimeSize.depth_m &&
            Spawn.Actual.position == Regiment->pose.position);
    }
    return !HasAnyErrors();
}

#endif

#endif
