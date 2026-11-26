#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentMissionScenario.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentFieldExtentTest, "RegimentBattle.BattlefieldSize.TacticalDeployment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentFieldExtentTest::RunTest(const FString& Parameters)
{
    FMissionPreset Preset;
    Preset.Field = {241, 151};
    Preset.Terrain = battle::MarchTerrain::OpenField;
    Preset.bDirectDeployment = true;
    for (auto& Regiment : Preset.Roster)
        Regiment.bEnabled = false;

    for (int32 Side = 0; Side < 2; ++Side)
    {
        auto& Regiment = Preset.Roster[Side * MissionRegimentsPerTeam];
        Regiment = {18, 4, 8};
        Regiment.StartX = Side == 0 ? -85.5f : 85.5f;
        Regiment.StartY = 65.5f;
    }

    FText Error;
    if (!TestTrue(TEXT("An authored start outside the legacy board is valid on a larger field"),
        RegimentMissionPresets::Validate(Preset, Error)))
        return false;

    const auto Opening = MakeMissionOpening(Preset.Roster, Preset.Terrain, Preset.Field);
    const auto Scenario = BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart);
    if (!TestEqual(TEXT("Both companies are deployed"), int32(Scenario.regiments.size()), 2))
        return false;
    TestEqual(TEXT("First company uses the specified coordinates"), Scenario.regiments[0].pose.position.x, -85.5f);
    TestEqual(TEXT("Second company uses the specified coordinates"), Scenario.regiments[1].pose.position.y, 65.5f);
    TestTrue(TEXT("Rally station lies inside the new board, behind the regiment"),
        Scenario.regiments[0].rally_point.x < -85.5f && Scenario.regiments[0].rally_point.x > -Preset.Field.HalfWidth());
    TestTrue(TEXT("The authored deployment creates a valid simulation"), bool(battle::CreateWorld(Scenario, 42)));

    const auto Navigation = MakeMissionNavigation(Scenario.obstacles, Preset.Field);
    const battle::Pose Start{{-85.5f, 65.5f, 0.f}, 0.f};
    TestTrue(TEXT("Tactical paths outside the old field can use the expanded board"),
        Navigation.FindPath({Start, {85.5f, 65.5f, 0.f}, {4.f, 3.f}}).status == battle::Status::Ok);
    TestTrue(TEXT("Tactical paths cannot leave the expanded board"),
        Navigation.FindPath({Start, {-85.5f, 85.f, 0.f}, {4.f, 3.f}}).status != battle::Status::Ok);

    auto Contact = Opening;
    Contact[0].pose.position.x = -86.f;
    const auto AtContact = BuildPresetBattle(Preset, Contact, EMissionDeployment::MarchContact);
    if (TestEqual(TEXT("Both companies deploy from march contact"), int32(AtContact.regiments.size()), 2))
        TestEqual(TEXT("Contact snaps to the same half-meter cell as pathfinding"),
            AtContact.regiments[0].pose.position.x, -85.5f);

    Preset.Field = {80, 60};
    Preset.Roster = MakeDefaultMissionRoster();
    Preset.Terrain = battle::MarchTerrain::River;
    const auto Compact = MakeMissionOpening(Preset.Roster, Preset.Terrain, Preset.Field);
    TestTrue(TEXT("Automatic positions fit the smaller river field"), RegimentMissionPresets::Validate(Preset, Error));
    TestTrue(TEXT("Automatic positions use the new depth without changing crossing locations"),
        Compact.front().pose.position.x == -26.f && Compact.front().pose.position.y == battle::MarchCrossings[0].y);
    return !HasAnyErrors();
}
#endif