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
        TestFalse(TEXT("Every commander has a displayed tactical rating"), Card.TacticalSkill.IsEmpty());
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
        const TCHAR* OrangeCommander = nullptr;
    } HistoricalFixtures[] = {
        {FGuid(0xB60CD12C, 0xBABE4FF6, 0x894C48EC, 0xB4F96421), TEXT("Филипп VI Валуа")},
        {FGuid(0x6DC62DF8, 0x9CFA521A, 0xB4829690, 0x956C5B22), TEXT("Наполеон Бонапарт")},
        {FGuid(0x845BCC52, 0xB6655AA6, 0xA8E31464, 0x0A1E99B2), TEXT("Карл XII"), TEXT("Шарль Эжен де Круа")},
    };
    for (const auto& Fixture : HistoricalFixtures)
    {
        if (!TestTrue(TEXT("Historical campaign stage loads"), RegimentMissionPresets::LoadBuiltIn(Fixture.Mission, Preset)))
            return false;
        const auto Stage = BuildPresetBattle(Preset,
            MakeMissionOpening(Preset.Roster, Preset.Terrain, Preset.Field, Preset.FigureTarget),
            EMissionDeployment::AuthoredStart);
        if (!TestTrue(TEXT("Historical campaign stage has a command hierarchy"), Stage.command_setup.has_value()))
            return false;
        const auto StageWorld = battle::CreateWorld(Stage, 42);
        if (!TestTrue(TEXT("Historical fixture creates a world"), bool(StageWorld)))
            return false;
        const TCHAR* Names[] = {Fixture.BlueCommander, Fixture.OrangeCommander};
        for (int32 Team = 0; Team < UE_ARRAY_COUNT(Names); ++Team)
        {
            if (!Names[Team])
                continue;
            const auto& Nodes = Stage.command_setup->hierarchy.nodes;
            const auto Root = std::ranges::find_if(Nodes, [Team](const auto& Node)
            {
                return Node.level == battle::command::Level::Army && Node.team == Team;
            });
            if (!TestTrue(TEXT("Historical fixture has an army commander"), Root != Nodes.end()))
                return false;
            const auto Card = Describe(StageWorld.value, Root->commander, Preset.Era.Id);
            TestEqual(TEXT("Scenario uses the named army commander"), Card.Name, FString(Names[Team]));
            TestNotNull(TEXT("The named commander has a portrait"), Card.Portrait);
        }
    }
    Preset.HistoricalIdentity.Invalidate();
    const auto CustomOpening = MakeMissionOpening(Preset.Roster, Preset.Terrain, Preset.Field, Preset.FigureTarget);
    const auto CustomScenario = BuildPresetBattle(Preset, CustomOpening, EMissionDeployment::AuthoredStart);
    const auto CustomBattle = battle::CreateWorld(CustomScenario, 42);
    if (!TestTrue(TEXT("A battle without historical commander overrides creates a world"), bool(CustomBattle)))
        return false;
    TSet<UTexture2D*> CustomArmyPortraits;
    for (const auto& Node : CustomScenario.command_setup->hierarchy.nodes)
    {
        if (Node.level != battle::command::Level::Army)
            continue;
        const auto Card = Describe(CustomBattle.value, Node.commander);
        TestNotNull(TEXT("A generated army commander has a portrait"), Card.Portrait);
        TestFalse(TEXT("A generated army commander has trait effects"), Card.Effects.IsEmpty());
        TestEqual(TEXT("Generated portraits are stable on repeated reads"),
            Describe(CustomBattle.value, Node.commander).Portrait, Card.Portrait);
        CustomArmyPortraits.Add(Card.Portrait);
    }
    TestEqual(TEXT("Generated opposing commanders have their own portraits"), CustomArmyPortraits.Num(), 2);
    return true;
}
#endif
