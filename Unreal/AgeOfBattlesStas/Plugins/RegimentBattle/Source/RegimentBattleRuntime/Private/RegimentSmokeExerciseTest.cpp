#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentGameModules.h"
#include "RegimentMissionScenario.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Simulation.h"
#include "battle/command/Services.h"
THIRD_PARTY_INCLUDES_END

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentSmokeExerciseTest, "RegimentBattle.SmokeExercise",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentSmokeExerciseTest::RunTest(const FString&)
{
    const FGuid Id(0x8A5DF38B, 0x7650418A, 0x96349670, 0x0BAFEC20);
    const auto Contains = [Id](const TArray<FMissionPresetListItem>& Items)
    {
        return Items.ContainsByPredicate([Id](const auto& Item) { return Item.Id == Id; });
    };
    TestTrue(TEXT("The smoke exercise is selectable in the proving ground"),
        Contains(RegimentGameModules::Presets({}, FString())));
    TestFalse(TEXT("The historical modules do not acquire a gunpowder preset"),
        Contains(RegimentGameModules::Presets(TEXT("ancient"), FString())) ||
        Contains(RegimentGameModules::Presets(TEXT("high_medieval"), FString())));

    FMissionPreset Preset;
    if (!TestTrue(TEXT("The shipped exercise opens"), RegimentMissionPresets::LoadBuiltIn(Id, Preset)))
        return false;
    FText Error;
    if (!TestTrue(TEXT("The exercise starts directly and has a valid neutral preset"),
        Preset.StartsInBattle() && !Preset.Era.IsSet() && RegimentMissionPresets::Validate(Preset, Error)))
        return false;

    const auto Opening = MakeMissionOpening(Preset.Roster, Preset.Terrain, Preset.Field, Preset.FigureTarget);
    const auto Scenario = BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart);
    auto Created = battle::CreateWorld(Scenario, 42);
    if (!TestTrue(TEXT("BattleCore accepts the proving ground"), bool(Created)))
        return false;
    auto& World = Created.value;
    const auto Navigation = MakeMissionNavigation(Scenario.obstacles, Preset.Field);
    bool Ignited = false;
    for (int32 Tick = 0; Tick < 180 && World.outcome == battle::BattleOutcome::Running && !Ignited; ++Tick)
    {
        if (!TestTrue(TEXT("The exercise simulates a battle tick"), bool(battle::Step(World, {}, {}, Navigation))))
            return false;
        Ignited = std::ranges::any_of(World.events,
            [](const auto& Event) { return Event.kind == battle::EventKind::SmokeCreated; });
    }
    if (!TestTrue(TEXT("An ordinary opening shot actually generates a smoke cloud"), Ignited && !World.smoke.empty()))
        return false;

    const auto& Hierarchy = World.command_state->hierarchy;
    const auto Observer = std::ranges::find(Hierarchy.nodes, battle::RegimentId{1}, &battle::command::CommandNode::regiment);
    if (!TestTrue(TEXT("The smoke exercise has an observing field officer"), Observer != Hierarchy.nodes.end()))
        return false;
    const auto Officer = std::ranges::find(World.officers, Observer->commander, &battle::OfficerSpec::id);
    if (!TestTrue(TEXT("The observer is on the field"), Officer != World.officers.end()))
        return false;
    const auto* Eye = battle::FindUnit(World, Officer->unit);
    const battle::RegimentId EnemyId{MissionRegimentsPerTeam + 1};
    const auto* Enemy = battle::FindRegiment(World, EnemyId);
    if (!TestTrue(TEXT("The two formations are available for a sight test"), Eye && Enemy))
        return false;
    const auto CanSeeEnemy = [&]
    {
        const auto Situation = battle::command::BuildSituation(World, Hierarchy, Observer->id, World.tick);
        return Situation.value && std::ranges::any_of(Situation.value->contacts,
            [EnemyId](const auto& Contact) { return Contact.regiment == EnemyId; });
    };

    World.smoke.resize(1);
    auto& Cloud = World.smoke.front();
    const auto Center = (Eye->pose.position + Enemy->pose.position) * .5f;
    Cloud.center = Center + battle::Vec3{0.f, 100.f, 0.f};
    TestTrue(TEXT("A live cloud away from this line leaves the enemy visible"), CanSeeEnemy());
    Cloud.center = Center;
    TestFalse(TEXT("The same cloud on the line obscures the enemy"), CanSeeEnemy());
    Cloud.expires_tick = World.tick;
    TestTrue(TEXT("The view resumes when the cloud expires"), CanSeeEnemy());
    return !HasAnyErrors();
}
#endif
