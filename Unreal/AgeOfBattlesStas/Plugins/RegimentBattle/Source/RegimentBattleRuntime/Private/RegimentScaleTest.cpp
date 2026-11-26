#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentMissionScenario.h"
#include "battle/Simulation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentScaleTest, "RegimentBattle.MissionPresets.TenThousand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentScaleTest::RunTest(const FString& Parameters)
{
    FMissionPreset Preset;
    const FGuid Standard(0xBD8E7423, 0xCDA541E0, 0xAD6DAF78, 0xD4C9F547);
    const FGuid Large(0x39FA5E9A, 0x273F4DCE, 0xB950627B, 0xFA9D8921);
    if (!TestTrue(TEXT("The normal historical mission still loads"), RegimentMissionPresets::LoadBuiltIn(Standard, Preset)))
        return false;
    auto Opening = MakeMissionOpening(Preset);
    auto Scenario = BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart);
    TestEqual(TEXT("The historical preset uses the standard scale"), int32(Scenario.units.size()), MissionStandardFigureTarget);
    TestTrue(TEXT("The historical preset uses the large-battle profile"), Scenario.rules.adaptive_battle);

    if (!TestTrue(TEXT("The large battle is a built-in mission"), RegimentMissionPresets::LoadBuiltIn(Large, Preset)))
        return false;
    FText Error;
    TestTrue(TEXT("The built-in workload validates"), RegimentMissionPresets::Validate(Preset, Error));
    TestEqual(TEXT("The built-in scenario requests ten thousand figures"), Preset.FigureTarget, 10000);
    TestTrue(TEXT("The test starts with the authored deployment"), Preset.StartsInBattle());
    Opening = MakeMissionOpening(Preset);
    Scenario = BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart);
    TestEqual(TEXT("The simulation contains ten thousand figures, including the staff"), int32(Scenario.units.size()), 10000);
    TestEqual(TEXT("The command staff includes two mounted escorts and courier pools"),
        int32(Scenario.officers.size()), 51);
    const auto& Staff = Scenario.command_setup->hierarchy.support;
    using battle::command::SupportDuty;
    TestEqual(TEXT("Two adjutants are assigned per army"), int32(std::ranges::count(Staff, SupportDuty::Adjutant,
        &battle::command::SupportAssignment::duty)), 4);
    TestEqual(TEXT("Three bodyguards are assigned per army"), int32(std::ranges::count(Staff, SupportDuty::Bodyguard,
        &battle::command::SupportAssignment::duty)), 6);
    TestEqual(TEXT("Keeps the 11 separately commanded regiments"), int32(Scenario.regiments.size()), 11);
    TestTrue(TEXT("Uses the adaptive simulation profile"), Scenario.rules.adaptive_battle);
    auto World = battle::CreateWorld(Scenario, 42);
    if (!TestTrue(TEXT("The wide deployment, soldier IDs, and officer hierarchy are valid"), bool(World)))
        return false;
    auto& State = World.value;
    const auto Nav = MakeMissionNavigation(Scenario.obstacles, Preset.Field);
    bool bMoved = false;
    bool bHit = false;
    for (int32 Step = 0; Step < 800 && !bHit; ++Step)
    {
        if (!TestTrue(TEXT("The large battle advances"), bool(battle::Step(State, {}, {}, Nav))))
            return false;
        bMoved |= std::abs(State.regiments.front().pose.position.x - Scenario.regiments.front().pose.position.x) > 1.f;
        bHit = std::ranges::any_of(State.events, [](const auto& Event) { return Event.kind == battle::EventKind::Hit && !Event.projectile; });
    }
    TestTrue(TEXT("Infantry advances from its starting position"), bMoved);
    TestTrue(TEXT("Opponents reach melee and land a hit within forty simulation seconds"), bHit);

    if (bHit)
        AddInfo(FString::Printf(TEXT("Cannae10k first melee hit at tick %llu"), State.tick));

    FMissionPreset TooSmall = Preset;
    TooSmall.Terrain = battle::MarchTerrain::OpenField;
    TooSmall.Field = {80, 60};
    for (auto& Entry : TooSmall.Roster)
    {
        Entry = FMissionRosterEntry(0, 0);
        Entry.bEnabled = false;
    }
    TooSmall.Roster[0].bEnabled = true;
    TooSmall.Roster[0].bCavalry = true;
    TooSmall.Roster[0].Mounted = 97;
    TooSmall.Roster[MissionRegimentsPerTeam].bEnabled = true;
    TestFalse(TEXT("Rejects an authored cavalry formation larger than the map"),
        RegimentMissionPresets::Validate(TooSmall, Error));
    TestTrue(TEXT("Explains why the enlarged formation cannot deploy"),
        Error.ToString().Contains(TEXT("не помещается")));

    return !HasAnyErrors();
}
#endif
