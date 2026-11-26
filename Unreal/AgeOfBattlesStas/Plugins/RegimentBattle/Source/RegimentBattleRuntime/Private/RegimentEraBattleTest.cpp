#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentEraCatalog.h"
#include "RegimentMissionScenario.h"
#include "battle/Snapshot.h"
#include "battle/Simulation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentEraBattleTest, "RegimentBattle.Eras.PlayableBattles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentEraBattleTest::RunTest(const FString& Parameters)
{
    for (const TCHAR* EraId : {TEXT("ancient"), TEXT("high_medieval")})
    {
        const auto* Era = RegimentEraCatalog::Find(EraId);
        if (!TestNotNull(EraId, Era))
            continue;

        FMissionPreset Preset;
        if (!TestTrue(TEXT("The era scenario appears in the mission editor"),
            RegimentMissionPresets::LoadBuiltIn(Era->MissionId, Preset)))
            continue;

        FText Error;
        TestTrue(TEXT("The authored mission is valid"), RegimentMissionPresets::Validate(Preset, Error));
        const auto Opening = MakeMissionOpening(Preset.Roster, Preset.Terrain);
        const auto Scenario = BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart);
        TestEqual(TEXT("The era rule profile drives the simulation"), Scenario.rules.panic_fraction, Era->Rules.panic_fraction);
        TestEqual(TEXT("The era rule profile specifies ranged cohorts"), Scenario.rules.ranged_cohorts, Era->Rules.ranged_cohorts);
        TestEqual(TEXT("The default scenario deploys three regiments per army"), int32(Scenario.regiments.size()), 6);

        const auto* Blue = RegimentEraCatalog::FindFaction(*Era, Preset.Era.BlueFaction);
        const auto* Orange = RegimentEraCatalog::FindFaction(*Era, Preset.Era.OrangeFaction);
        if (!TestNotNull(TEXT("First faction resolves"), Blue) || !TestNotNull(TEXT("Second faction resolves"), Orange))
            continue;

        const auto CheckUnit = [&](const battle::Scenario& Candidate, size_t Index, const FRegimentEraFaction& Faction)
        {
            if (!TestTrue(TEXT("There is a frontline figure at the tested index"), Index < Candidate.units.size()))
                return;
            const auto& Unit = Candidate.units[Index];
            const auto& Profile = Faction.Troops[int32(ERegimentEraRole::Spearmen)];
            TestEqual(TEXT("Unit receives its era archetype name"), FString(UTF8_TO_TCHAR(Unit.name.c_str())), Profile.Name);
            TestEqual(TEXT("Unit receives armour from its army's archetype"), Unit.armour, Profile.Armour);
            TestEqual(TEXT("Unit receives weapon reach from the archetype"), Unit.weapon.range, Profile.Weapon.range);
        };
        CheckUnit(Scenario, 0, *Blue);
        Swap(Preset.Era.BlueFaction, Preset.Era.OrangeFaction);
        CheckUnit(BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart), 0, *Orange);

        auto First = battle::CreateWorld(Scenario, 42);
        auto Second = battle::CreateWorld(Scenario, 42);
        if (!TestTrue(TEXT("BattleCore accepts the era's battlefield"), bool(First) && bool(Second)))
            continue;
        const battle::GridNavigationService Navigation(Scenario.obstacles, 1.f, 121, 77, {-60.f, -38.f, 0.f});
        for (int32 Step = 0; Step < 60; ++Step)
        {
            if (!TestTrue(TEXT("Both simulations advance"), bool(battle::Step(First.value, {}, {}, Navigation)) &&
                    bool(battle::Step(Second.value, {}, {}, Navigation))))
                break;
        }
        TestTrue(TEXT("The new data gives a repeatable real-time battle"),
            battle::StateHash(First.value) == battle::StateHash(Second.value));
    }
    return !HasAnyErrors();
}
#endif