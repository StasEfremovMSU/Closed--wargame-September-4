#include "RegimentGameModules.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Algo/Count.h"
#include "Algo/Find.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "RegimentEraCatalog.h"
#include "RegimentMissionScenario.h"
#include "battle/Formation.h"
#include "battle/Simulation.h"

namespace
{
const FGuid Cannae(0xBD8E7423, 0xCDA541E0, 0xAD6DAF78, 0xD4C9F547);
const FGuid AncientPlatforms(0x6C326E44, 0xB6484ABB, 0x893D83EE, 0xDB4F9D6F);
const FGuid Peipus(0x8E7D530F, 0x47E5433E, 0x92ED12CA, 0x13FD89C8);
const FGuid Stirling(0x0C79AE21, 0xF7BC42D8, 0x91DE32A5, 0x598C19E9);
const FGuid Hill(0xA16D79C4, 0xB2474F66, 0x98D7143E, 0x622FCE91);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentGameModuleCatalogTest, "RegimentBattle.GameModules.Catalog",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentGameModuleCatalogTest::RunTest(const FString& Parameters)
{
    const auto Modules = RegimentGameModules::List();
    TestFalse(TEXT("The archived fantasy era is absent from module selection"),
        Modules.ContainsByPredicate([](const auto& Module) { return Module.Id == TEXT("fantasy"); }));
    const TCHAR* Ids[] = {
        TEXT("ancient"), TEXT("high_medieval"), TEXT("late_medieval"), TEXT("renaissance"),
        TEXT("sengoku"), TEXT("great_northern_war"), TEXT("napoleonic"),
    };
    constexpr int32 FactionCounts[] = {13, 14, 11, 10, 8, 5, 9};
    TestEqual(TEXT("Only the seven historical modules appear"), Modules.Num(), int32(UE_ARRAY_COUNT(Ids)));
    for (int32 Index = 0; Index < FMath::Min(Modules.Num(), int32(UE_ARRAY_COUNT(Ids))); ++Index)
    {
        TestEqual(TEXT("Module order follows the documented catalog"), Modules[Index].Id, FString(Ids[Index]));
        TestTrue(FString(Ids[Index]) + TEXT(" opens from the module menu"), Modules[Index].bAvailable);
        TestEqual(FString(Ids[Index]) + TEXT(" exposes every agreed faction"), Modules[Index].FactionCount, FactionCounts[Index]);
    }

    FMissionPreset Preset;
    TestTrue(TEXT("Ancient starts its own authored battle"), RegimentGameModules::Open(TEXT("ancient"), Preset));
    TestEqual(TEXT("The Ancient opening keeps its module"), Preset.Era.Id, FString(TEXT("ancient")));
    TestTrue(TEXT("Medieval starts its own authored battle"), RegimentGameModules::Open(TEXT("high_medieval"), Preset));
    TestEqual(TEXT("The Medieval opening keeps its module"), Preset.Era.Id, FString(TEXT("high_medieval")));
    TestFalse(TEXT("The archived era cannot be opened by id"), RegimentGameModules::Open(TEXT("fantasy"), Preset));
    TestTrue(TEXT("The archived era has no playable preset list"),
        RegimentGameModules::Presets(TEXT("fantasy"), RegimentMissionPresets::Directory()).IsEmpty());
    TestFalse(TEXT("An unknown module cannot open"), RegimentGameModules::Open(TEXT("unknown"), Preset));
    TestEqual(TEXT("Failed opens leave the setup intact"), Preset.Era.Id, FString(TEXT("high_medieval")));
    TestTrue(TEXT("The generic path opens the independent default"), RegimentGameModules::Open({}, Preset));
    TestFalse(TEXT("The generic default has no era"), Preset.Era.IsSet());
    TestEqual(TEXT("The separate generic label"), RegimentGameModules::Name({}).ToString(), FString(TEXT("Полигон")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentGameModuleScopeTest, "RegimentBattle.GameModules.Scope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentGameModuleScopeTest::RunTest(const FString& Parameters)
{
    const FString Directory = FPaths::ProjectSavedDir() / (TEXT("ModuleScopeTest-") + FGuid::NewGuid().ToString());
    struct FSavedCopies
    {
        const FString& Directory;
        TArray<FGuid> Ids;
        ~FSavedCopies()
        {
            auto& Files = IFileManager::Get();
            for (FGuid Id : Ids)
                Files.Delete(*(Directory / (Id.ToString(EGuidFormats::Digits) + TEXT(".json"))));
            Files.DeleteDirectory(*Directory);
        }
    } Saves{Directory, {}};

    FText Error;
    const FStringView Ancient = TEXT("ancient");
    const FStringView Medieval = TEXT("high_medieval");
    const FStringView Scopes[] = {{}, Ancient, Medieval};
    for (const FStringView Scope : Scopes)
    {
        FMissionPreset Draft;
        FGuid Saved;
        if (!TestTrue(TEXT("Opens scope to make a saved copy"), RegimentGameModules::Open(Scope, Draft)))
            return false;
        Draft.Name = FString(TEXT("Saved copy ")) + FString(Scope) + TEXT(" generic-compatible");
        if (!TestTrue(TEXT("Saves a scoped copy"), RegimentMissionPresets::SaveCopy(Directory, Draft, Saved, Error)))
        {
            AddError(Error.ToString());
            return false;
        }
        Saves.Ids.Add(Saved);
    }

    const auto GenericMissions = RegimentGameModules::Presets({}, Directory);
    const auto AncientMissions = RegimentGameModules::Presets(Ancient, Directory);
    const auto MedievalMissions = RegimentGameModules::Presets(Medieval, Directory);
    const auto Find = [](const auto& Items, FGuid Id) {
        return Algo::FindBy(Items, Id, &FMissionPresetListItem::Id);
    };
    TestNotNull(TEXT("Legacy Stirling stays in the generic path"), Find(GenericMissions, Stirling));
    TestNull(TEXT("Ancient excludes Stirling"), Find(AncientMissions, Stirling));
    TestNull(TEXT("Medieval excludes Stirling"), Find(MedievalMissions, Stirling));
    TestNotNull(TEXT("Ancient includes Cannae"), Find(AncientMissions, Cannae));
    TestNotNull(TEXT("Ancient includes the platform exercise"), Find(AncientMissions, AncientPlatforms));
    TestNull(TEXT("Medieval excludes the Ancient platform exercise"), Find(MedievalMissions, AncientPlatforms));
    TestNull(TEXT("Generic excludes the Ancient platform exercise"), Find(GenericMissions, AncientPlatforms));
    TestNotNull(TEXT("Medieval includes Peipus"), Find(MedievalMissions, Peipus));
    TestNull(TEXT("Ancient excludes Peipus"), Find(AncientMissions, Peipus));
    TestNull(TEXT("Medieval excludes Cannae"), Find(MedievalMissions, Cannae));
    TestNull(TEXT("Generic excludes Ancient history"), Find(GenericMissions, Cannae));
    TestNull(TEXT("Generic excludes Medieval history"), Find(GenericMissions, Peipus));
    TestTrue(TEXT("Unknown module has no preset menu"),
        RegimentGameModules::Presets(TEXT("unknown"), Directory).IsEmpty());

    const TArray<FMissionPresetListItem>* Lists[] = {&GenericMissions, &AncientMissions, &MedievalMissions};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Scopes); ++Index)
    {
        const auto& List = *Lists[Index];
        for (int32 Copy = 0; Copy < Saves.Ids.Num(); ++Copy)
            TestEqual(TEXT("Only the active scope's saved copy appears"), Find(List, Saves.Ids[Copy]) != nullptr, Copy == Index);
        const auto* Shared = Find(List, Hill);
        if (!TestNotNull(TEXT("The hill exercise appears in every scope"), Shared))
            return false;
        FMissionPreset Adapted;
        TestTrue(TEXT("The hill exercise loads in the active scope"),
            RegimentGameModules::Load(Scopes[Index], Directory, *Shared, Adapted, Error));
        TestTrue(TEXT("The shared exercise keeps its objective"), Adapted.Objective.Goal == EMissionGoal::HillHold);
        TestTrue(TEXT("The shared exercise uses the selected era"), Scopes[Index].Equals(Adapted.Era.Id));
        TestFalse(TEXT("Shared built-ins keep their read-only identity"), Adapted.EditableCopyId.IsValid());
    }

    const auto* AncientCopy = Find(AncientMissions, Saves.Ids[1]);
    const auto* AncientHistory = Find(AncientMissions, Cannae);
    if (!AncientCopy || !AncientHistory)
        return false;
    FMissionPreset Kept;
    TestTrue(TEXT("An Ancient copy reloads unchanged"),
        RegimentGameModules::Load(Ancient, Directory, *AncientCopy, Kept, Error));
    TestEqual(TEXT("The saved identity survives matching scope load"), Kept.EditableCopyId, Saves.Ids[1]);
    TestFalse(TEXT("Foreign saved data cannot bypass the listing filter"),
        RegimentGameModules::Load(Medieval, Directory, *AncientCopy, Kept, Error));
    TestFalse(TEXT("Foreign built-in data cannot bypass the listing filter"),
        RegimentGameModules::Load(Medieval, Directory, *AncientHistory, Kept, Error));
    TestFalse(TEXT("Unknown scope cannot load even a correctly registered history item"),
        RegimentGameModules::Load(TEXT("unknown"), Directory, *AncientHistory, Kept, Error));
    TestFalse(TEXT("Failure yields an explanation"), Error.IsEmpty());
    TestEqual(TEXT("Failed cross-module loads preserve the current setup"), Kept.Era.Id, FString(Ancient));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentEraMissionPortfolioTest, "RegimentBattle.GameModules.EraMissionPortfolio",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentEraMissionPortfolioTest::RunTest(const FString&)
{
    const FString Directory = FPaths::ProjectSavedDir() / (TEXT("EraPortfolio-") + FGuid::NewGuid().ToString());
    for (const auto& Module : RegimentGameModules::List())
    {
        const auto* Era = RegimentEraCatalog::Find(Module.Id);
        if (!TestNotNull(*Module.Id, Era))
            continue;
        const auto Missions = RegimentGameModules::Presets(Module.Id, Directory);
        int32 AuthoredMissions = 0;
        for (const auto& Item : Missions)
        {
            if (!Item.bBuiltIn || Item.bSharedAcrossModules || Item.EraId != Module.Id)
                continue;
            ++AuthoredMissions;
            const FString Label = Module.Id + TEXT(" / ") + Item.Name;
            FMissionPreset Preset;
            FText Error;
            if (!TestTrue(Label + TEXT(" opens in this module"),
                    RegimentGameModules::Load(Module.Id, Directory, Item, Preset, Error)))
                continue;
            if (!TestTrue(Label + TEXT(" validates"), RegimentMissionPresets::Validate(Preset, Error)))
            {
                AddError(Error.ToString());
                continue;
            }
            const auto Opening = MakeMissionOpening(Preset.Roster, Preset.Terrain, Preset.Field, Preset.FigureTarget);
            const auto Scenario = BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart);
            auto World = battle::CreateWorld(Scenario, 42);
            if (!TestTrue(Label + TEXT(" creates a BattleCore world"), bool(World)))
                continue;
            TestEqual(Label + TEXT(" uses its era discipline"), Scenario.rules.panic_fraction, Era->Rules.panic_fraction);
            if (Preset.FigureTarget < MissionMaxFigureTarget)
            {
                const auto Nav = MakeMissionNavigation(Scenario.obstacles, Preset.Field);
                for (int32 Tick = 0; Tick < 40; ++Tick)
                    if (!TestTrue(Label + TEXT(" advances in real time"), bool(battle::Step(World.value, {}, {}, Nav))))
                        break;
            }
            if (Item.Id != Era->MissionId)
                continue;
            Preset.Name = TEXT("Проверка копии");
            FGuid Copy;
            if (TestTrue(Label + TEXT(" saves an edited copy"),
                    RegimentMissionPresets::SaveCopy(Directory, Preset, Copy, Error)))
            {
                const auto Copies = RegimentMissionPresets::ListForEra(Directory, Module.Id);
                TestNotNull(Label + TEXT(" lists the edited copy in its era"),
                    Algo::FindBy(Copies, Copy, &FMissionPresetListItem::Id));
                FMissionPreset Loaded;
                TestTrue(Label + TEXT(" reloads the edited copy"),
                    RegimentMissionPresets::Load(Directory, Copy, Loaded, Error));
                TestEqual(Label + TEXT(" keeps the era after saving"), Loaded.Era.Id, Module.Id);
                IFileManager::Get().Delete(*(Directory / (Copy.ToString(EGuidFormats::Digits) + TEXT(".json"))));
            }
        }
        TestTrue(Module.Id + TEXT(" has at least two era missions"), AuthoredMissions >= 2);
    }
    IFileManager::Get().DeleteDirectory(*Directory);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentPlayableHistoryTest, "RegimentBattle.GameModules.PlayableHistory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentPlayableHistoryTest::RunTest(const FString&)
{
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("PlayableHistoryTest");
    TSet<FGuid> HistoricalIds;
    for (const auto& Module : RegimentGameModules::List())
    {
        FMissionPreset Startup;
        if (!TestTrue(Module.Id + TEXT(" opens through the game menu"), RegimentGameModules::OpenPlayable(Module.Id, Startup)))
            continue;
        TestEqual(Module.Id + TEXT(" opens at the standard scale"), Startup.FigureTarget, MissionStandardFigureTarget);
        const auto Items = RegimentGameModules::PlayablePresets(Module.Id, Directory);
        int32 ModuleHistoricalCount = 0;
        const auto* Range = Algo::FindBy(Items, AncientPlatforms, &FMissionPresetListItem::Id);
        TestEqual(Module.Id + TEXT(" shows the siege practice only in Antiquity"),
            Range && !Range->bHistorical, Module.Id == TEXT("ancient"));
        for (const auto& Item : Items)
        {
            if (!Item.bBuiltIn || Item.Id == AncientPlatforms)
                continue;
            if (!TestTrue(Item.Name + TEXT(" is a historical battle"), Item.bHistorical))
                continue;
            ++ModuleHistoricalCount;
            TestFalse(Item.Name + TEXT(" has a unique catalogue ID"), HistoricalIds.Contains(Item.Id));
            HistoricalIds.Add(Item.Id);
            FText Error;
            FMissionPreset Preset;
            if (!TestTrue(Item.Name + TEXT(" loads from the menu"),
                    RegimentGameModules::LoadPlayable(Module.Id, Directory, Item, Preset, Error)))
                continue;
            TestTrue(Item.Name + TEXT(" has an authored briefing and sources"),
                !Preset.Briefing.IsEmpty() && !Preset.Sources.IsEmpty());
            TestTrue(Item.Name + TEXT(" opens at authored positions"), Preset.StartsInBattle());
            const auto Opening = MakeMissionOpening(Preset.Roster, Preset.Terrain, Preset.Field, Preset.FigureTarget);
            const auto Scenario = BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart);
            TestEqual(Item.Name + TEXT(" deploys the whole roster"), int32(Scenario.units.size()), MissionStandardFigureTarget);
            const int32 EnabledRegiments = Algo::CountIf(Preset.Roster, [](const auto& Entry) { return Entry.bEnabled; });
            TestEqual(Item.Name + TEXT(" deploys every enabled regiment"), int32(Scenario.regiments.size()), EnabledRegiments);
            if (Preset.Objective.Goal != EMissionGoal::Eliminate)
                TestTrue(Item.Name + TEXT(" has a tactical objective"), Scenario.objective.has_value());
            TestTrue(Item.Name + TEXT(" starts with valid forces"), bool(battle::CreateWorld(Scenario, 42)));
        }
        TestTrue(Module.Id + TEXT(" offers at least five historical battles"), ModuleHistoricalCount >= 5);
    }

    const auto Custom = RegimentGameModules::PlayablePresets(FStringView{}, Directory);
    TestEqual(TEXT("The generic module keeps a custom battle and Stirling"), Custom.Num(), 2);
    const auto* Bridge = Algo::FindBy(Custom, Stirling, &FMissionPresetListItem::Id);
    TestTrue(TEXT("Stirling is still playable"), Bridge && Bridge->bHistorical);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentNapoleonicTacticsTest, "RegimentBattle.GameModules.NapoleonicTactics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentNapoleonicTacticsTest::RunTest(const FString&)
{
    const FStringView Scope = TEXT("napoleonic");
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("NapoleonicTacticsTest");
    const auto* Era = RegimentEraCatalog::Find(Scope);
    if (!TestNotNull(TEXT("Napoleonic rules are available"), Era))
        return false;
    const FGuid Waterloo(0x7E801C0A, 0x1E9941BD, 0x983E0DFE, 0x4DC07B33);
    const auto Missions = RegimentGameModules::Presets(Scope, Directory);
    for (const auto Id : {Era->MissionId, Waterloo})
    {
        const auto* Item = Algo::FindBy(Missions, Id, &FMissionPresetListItem::Id);
        if (!TestNotNull(TEXT("Napoleonic mission is listed"), Item))
            continue;
        FText Error;
        FMissionPreset Preset;
        if (!TestTrue(Item->Name + TEXT(" loads"), RegimentGameModules::Load(Scope, Directory, *Item, Preset, Error)))
            continue;
        const auto Opening = MakeMissionOpening(Preset.Roster, Preset.Terrain, Preset.Field, Preset.FigureTarget);
        const auto Scenario = BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart);
        auto Result = battle::CreateWorld(Scenario, 42);
        if (!TestTrue(Item->Name + TEXT(" creates a world"), bool(Result)))
            continue;
        auto& World = Result.value;
        const auto Navigation = MakeMissionNavigation(Scenario.obstacles, Preset.Field);
        const auto& Attackers = *battle::FindRegiment(World, {1});
        const auto Origin = Attackers.pose;
        if (Id == Waterloo)
        {
            TestEqual(TEXT("Waterloo has six formations per side"), int32(World.regiments.size()), 12);
            TestTrue(TEXT("Crest orders stay at ground-level coordinates"),
                Scenario.objective && Scenario.objective->center.x > 50.f &&
                Scenario.objective->center.z == 0.f);
            TestEqual(TEXT("Waterloo has 5,000 figures"), int32(World.units.size()), MissionStandardFigureTarget);
            TestFalse(TEXT("French central assault starts in column"), battle::IsLinearFiringLine(Attackers));
            TestTrue(TEXT("French flank starts in line"), battle::IsLinearFiringLine(*battle::FindRegiment(World, {2})));
            const auto& BritishFront = *battle::FindRegiment(World, {MissionRegimentsPerTeam + 1});
            const auto& BritishReserve = *battle::FindRegiment(World, {MissionRegimentsPerTeam + 4});
            TestTrue(TEXT("Both British echelons start in line"),
                battle::IsLinearFiringLine(BritishFront) && battle::IsLinearFiringLine(BritishReserve));
            TestTrue(TEXT("British second line deploys separately"), BritishReserve.pose.position.x > BritishFront.pose.position.x + 15.f);
        }
        float ColumnAdvance = 0.f;
        bool bColumnReachedMelee = false;
        int32 LineShots = 0;
        int32 AssaultShots = 0;
        int32 GunShots = 0;
        int32 GunHits = 0;
        int32 GunKills = 0;
        const int32 MaxTicks = Id == Waterloo ? 900 : 3000;
        for (int32 Tick = 0; Tick < MaxTicks && World.outcome == battle::BattleOutcome::Running; ++Tick)
        {
            if (!TestTrue(Item->Name + TEXT(" advances"), bool(battle::Step(World, {}, {}, Navigation))))
                break;
            const bool bInColumn = !battle::IsLinearFiringLine(Attackers);
            const bool bColumnAttack = bInColumn && Attackers.ai_action == battle::OrderType::Attack;
            if (bColumnAttack || (Id == Waterloo && bInColumn && Attackers.ai_action == battle::OrderType::MoveTo))
                ColumnAdvance = FMath::Max(ColumnAdvance,
                    battle::Dot(Attackers.pose.position - Origin.position, battle::Forward(Origin.yaw)));
            for (const auto& Event : World.events)
            {
                const auto* Source = battle::FindUnit(World, Event.source);
                if (Source && Source->platform == battle::BattlePlatform::FieldGun)
                {
                    GunShots += Event.kind == battle::EventKind::AttackLaunched;
                    GunHits += Event.kind == battle::EventKind::Hit;
                    GunKills += Event.kind == battle::EventKind::Died;
                }
                if (Event.kind != battle::EventKind::AttackLaunched)
                    continue;
                bColumnReachedMelee |= bColumnAttack && Event.regiment == Attackers.id && !Event.projectile;
                AssaultShots += bColumnAttack && Event.regiment == Attackers.id && Event.projectile &&
                    Source && Source->alive && !Source->skirmisher;
                if (!Event.projectile || !Source || Source->ranged_weapon != battle::RangedWeapon::Musket ||
                    Source->skirmisher)
                    continue;
                const auto* Regiment = battle::FindRegiment(World, Event.regiment);
                if (!Regiment)
                    continue;
                if (battle::IsLinearFiringLine(*Regiment) && Source->slot_index < Regiment->formation.columns)
                    ++LineShots;
            }
            if (Id == Waterloo && LineShots > 0 && ColumnAdvance > 20.f && GunKills > 0)
                break;
        }
        AddInfo(FString::Printf(TEXT("%s: firing line shots %d, assault column shots %d, column advanced %.1f m, melee %d"),
            *Item->Name, LineShots, AssaultShots, ColumnAdvance, bColumnReachedMelee));
        AddInfo(FString::Printf(TEXT("%s: field gun shots %d, hits %d, kills %d"),
            *Item->Name, GunShots, GunHits, GunKills));
        TestTrue(Item->Name + TEXT(" uses a firing line"), LineShots > 0);
        TestEqual(Item->Name + TEXT(" holds fire in the assault column"), AssaultShots, 0);
        TestTrue(Item->Name + TEXT(" advances its blue assault column across the field"), ColumnAdvance > 20.f);
        if (Id == Era->MissionId)
            TestTrue(Item->Name + TEXT(" the blue assault column reaches melee on the open field"),
                bColumnReachedMelee);
        TestTrue(Item->Name + TEXT(" field artillery causes actual casualties"),
            GunHits > 0 && GunKills > 0);
    }
    return !HasAnyErrors();
}
#endif
