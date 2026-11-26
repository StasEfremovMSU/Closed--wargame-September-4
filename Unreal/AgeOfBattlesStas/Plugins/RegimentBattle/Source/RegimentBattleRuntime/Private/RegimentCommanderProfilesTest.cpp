#include "RegimentCommanderProfiles.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "RegimentEraCatalog.h"
#include "RegimentMissionPreset.h"
#include "RegimentMissionScenario.h"
#include "battle/Officers.h"
#include "battle/World.h"
#include "battle/command/Services.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentCommanderProfilesTest, "RegimentBattle.CommanderProfiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegimentCommanderProfilesTest::RunTest(const FString& Parameters)
{
    using namespace RegimentCommanderProfiles;
    for (const auto& Era : RegimentEraCatalog::All())
        for (const auto& Faction : Era.Factions)
            TestTrue(Era.Id + TEXT("/") + Faction.Id, HasNamePool(Era.Id, Faction.Id));

    FMissionPreset Preset;
    if (!TestTrue(TEXT("Waterloo loads"), RegimentMissionPresets::LoadBuiltIn(
        FGuid(0x7E801C0A, 0x1E9941BD, 0x983E0DFE, 0x4DC07B33), Preset)))
        return false;
    const auto Opening = MakeMissionOpening(Preset.Roster, Preset.Terrain, Preset.Field, Preset.FigureTarget);
    const auto Scenario = BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart);
    const auto Repeat = BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart);
    if (!TestTrue(TEXT("Scenario has command staff"), Scenario.command_setup.has_value()))
        return false;
    TestTrue(TEXT("Seed is repeatable"), Scenario.units == Repeat.units &&
        Scenario.officers == Repeat.officers && Scenario.command_setup == Repeat.command_setup);
    auto World = battle::CreateWorld(Scenario, 42);
    if (!TestTrue(TEXT("Core accepts generated traits"), bool(World)))
        return false;
    const auto Baseline = BuildMissionBattle(Preset.Roster, Opening, Preset.Terrain,
        EMissionDeployment::AuthoredStart, Preset.bRegimentTactics, Preset.Field,
        Preset.FigureTarget, Scenario.rules.linear_infantry, true);
    TSet<FString> ArmyNames;
    TSet<UTexture2D*> Portraits;
    TSet<FString> ProfilesByRank[4];
    bool bFoundSwift = false, bFoundSturdy = false, bFoundInspiring = false, bFoundDecisive = false;
    for (const auto& Node : Scenario.command_setup->hierarchy.nodes)
    {
        const auto Card = Describe(World.value, Node.commander);
        TestFalse(TEXT("Every commander has a name"), Card.Name.IsEmpty());
        TestFalse(TEXT("Every commander has a rank icon"), Card.RoleIcon.IsEmpty());
        TestFalse(TEXT("Every commander has visible effects"), Card.Effects.IsEmpty());
        const auto Officer = std::ranges::find(Scenario.officers, Node.commander, &battle::OfficerSpec::id);
        if (!TestTrue(TEXT("Commander has a simulation officer"), Officer != Scenario.officers.end()))
            return false;
        TestTrue(TEXT("Leadership traits stay bounded"), Officer->charisma >= 0.f && Officer->charisma <= 1.f);
        const auto Original = std::ranges::find(Baseline.units, Officer->unit, &battle::UnitSpec::id);
        auto* Figure = battle::FindUnit(World.value, Officer->unit);
        if (!TestTrue(TEXT("Commander has an original and a live figure"), Original != Baseline.units.end() && Figure))
            return false;
        const FString Effects = Card.Effects.ToString();
        const bool bSwift = Figure->speed > Original->speed && Figure->health < Original->health;
        const bool bSturdy = Figure->speed < Original->speed && Figure->health > Original->health;
        TestTrue(TEXT("Personal trait grants one advantage and one penalty"), bSwift || bSturdy);
        TestEqual(TEXT("Swift trait text matches movement"), Effects.Contains(TEXT("Подвижный")), bSwift);
        TestEqual(TEXT("Sturdy trait text matches endurance"), Effects.Contains(TEXT("Стойкий")), bSturdy);
        bFoundSwift |= bSwift;
        bFoundSturdy |= bSturdy;
        bFoundInspiring |= Effects.Contains(TEXT("Вдохновитель"));
        bFoundDecisive |= Effects.Contains(TEXT("Решительный"));
        ProfilesByRank[uint8(Node.level)].Add(Effects.Contains(TEXT("Быстрый")) ? TEXT("fast") : TEXT("deliberate"));
        if (Node.level == battle::command::Level::Regiment)
        {
            const auto* Regiment = battle::FindRegiment(World.value, Node.regiment);
            if (!TestNotNull(TEXT("Commander is attached to a regiment"), Regiment))
                return false;
            // The aura is conditional on the officer being within reach of the regiment center.
            const auto SavedPosition = Figure->pose.position;
            Figure->pose.position = Regiment->pose.position;
            const auto WithLeader = battle::GetStaffEffects(World.value, *Regiment);
            Figure->alive = false;
            const auto Unsupported = battle::GetStaffEffects(World.value, *Regiment);
            Figure->alive = true;
            Figure->pose.position = SavedPosition;
            TestTrue(TEXT("Regimental leadership lowers the panic threshold"),
                WithLeader.panic_fraction < Unsupported.panic_fraction);
        }
        if (Node.level == battle::command::Level::Army)
        {
            const auto& Runtime = *World.value.command_state;
            const auto NodeRuntime = std::ranges::find(Runtime.nodes, Node.id, &battle::command::NodeRuntime::node);
            if (!TestTrue(TEXT("Army chief has a runtime schedule"), NodeRuntime != Runtime.nodes.end()))
                return false;
            TestEqual(TEXT("Named army chiefs have opposite reevaluation styles"),
                Effects.Contains(TEXT("Быстрый штаб")), Card.Name == TEXT("Наполеон Бонапарт"));
            TestEqual(TEXT("Fast army trait uses a shorter runtime interval"),
                NodeRuntime->schedule.interval_ticks <= (Scenario.rules.adaptive_battle ? 104u : 40u),
                Card.Name == TEXT("Наполеон Бонапарт"));
            ArmyNames.Add(Card.Name);
            Portraits.Add(Card.Portrait);
            TestNotNull(TEXT("Named commander has a styled portrait"), Card.Portrait);
        }
    }
    TestTrue(TEXT("Napoleon has a fixed identity"), ArmyNames.Contains(TEXT("Наполеон Бонапарт")));
    TestTrue(TEXT("Wellington has a fixed identity"), ArmyNames.Contains(TEXT("Артур Уэлсли")));
    TestEqual(TEXT("Historical portraits differ"), Portraits.Num(), 2);
    TestTrue(TEXT("The roster contains opposed personal traits"), bFoundSwift && bFoundSturdy);
    TestTrue(TEXT("The roster contains opposed leadership traits"), bFoundInspiring && bFoundDecisive);
    for (const auto Rank : {battle::command::Level::Army, battle::command::Level::Group, battle::command::Level::Regiment})
        TestEqual(TEXT("Every command rank offers fast and deliberate styles"), ProfilesByRank[uint8(Rank)].Num(), 2);
    const struct
    {
        FGuid Mission;
        const TCHAR* BlueCommander;
    } CampaignFixtures[] = {
        {FGuid(0xB60CD12C, 0xBABE4FF6, 0x894C48EC, 0xB4F96421), TEXT("Филипп VI Валуа")},
        {FGuid(0x6DC62DF8, 0x9CFA521A, 0xB4829690, 0x956C5B22), TEXT("Наполеон Бонапарт")},
    };
    for (const auto& Fixture : CampaignFixtures)
    {
        if (!TestTrue(TEXT("Historical campaign stage loads"), RegimentMissionPresets::LoadBuiltIn(Fixture.Mission, Preset)))
            return false;
        const auto Stage = BuildPresetBattle(Preset,
            MakeMissionOpening(Preset.Roster, Preset.Terrain, Preset.Field, Preset.FigureTarget),
            EMissionDeployment::AuthoredStart);
        if (!TestTrue(TEXT("Historical campaign stage has a command hierarchy"), Stage.command_setup.has_value()))
            return false;
        const auto& Nodes = Stage.command_setup->hierarchy.nodes;
        const auto Root = std::ranges::find_if(Nodes, [](const auto& Node)
        {
            return Node.level == battle::command::Level::Army && Node.team == 0;
        });
        if (!TestTrue(TEXT("Historical campaign stage has an army commander"), Root != Nodes.end()))
            return false;
        const auto Commander = std::ranges::find(Stage.officers, Root->commander, &battle::OfficerSpec::id);
        if (!TestTrue(TEXT("Historical campaign commander has a staff entry"), Commander != Stage.officers.end()))
            return false;
        const auto Unit = std::ranges::find(Stage.units, Commander->unit, &battle::UnitSpec::id);
        if (!TestTrue(TEXT("Historical campaign commander has a figure"), Unit != Stage.units.end()))
            return false;
        TestEqual(TEXT("Campaign stage takes the historical commander"), FString(UTF8_TO_TCHAR(Unit->name.c_str())),
            FString(Fixture.BlueCommander));
    }
    return true;
}
#endif
