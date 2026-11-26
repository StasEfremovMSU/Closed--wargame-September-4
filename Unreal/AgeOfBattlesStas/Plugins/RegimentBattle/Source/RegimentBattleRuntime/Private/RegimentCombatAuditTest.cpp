#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RegimentBattlePlayback.h"
#include "RegimentGameModules.h"
#include "RegimentMissionProgress.h"
#include "RegimentMissionScenario.h"
#include "battle/Simulation.h"
#include <tuple>

namespace
{
enum class ETroopAuditStatus { NoAttacks, NoHits, NoDamage, NoKills, Kills };

struct FTroopAudit
{
    FString Profile;
    int32 Team = 0;
    int32 Deployed = 0;
    int32 Attacks = 0;
    int32 Hits = 0;
    double Damage = 0.0;
    int32 Kills = 0;
    ETroopAuditStatus Status() const
    {
        if (Attacks == 0)
            return ETroopAuditStatus::NoAttacks;
        if (Hits == 0)
            return ETroopAuditStatus::NoHits;
        if (Damage <= 0.0)
            return ETroopAuditStatus::NoDamage;
        return Kills == 0 ? ETroopAuditStatus::NoKills : ETroopAuditStatus::Kills;
    }
};

FString ProfileName(const battle::UnitState& Unit)
{
    if (!Unit.name.empty())
        return FString(UTF8_TO_TCHAR(Unit.name.c_str()));
    switch (Unit.platform)
    {
    case battle::BattlePlatform::IndianElephant: return TEXT("Индийский слон");
    case battle::BattlePlatform::AfricanElephant: return TEXT("Африканский слон");
    case battle::BattlePlatform::ScythedChariot: return TEXT("Колесница");
    case battle::BattlePlatform::Catapult: return TEXT("Катапульта");
    case battle::BattlePlatform::Ballista: return TEXT("Баллиста");
    case battle::BattlePlatform::FieldGun: return TEXT("Полевая пушка");
    default: break;
    }
    if (Unit.troop == battle::UnitTroop::Mounted)
        return TEXT("Кавалерия");
    if (Unit.archer)
        return TEXT("Стрелки");
    return Unit.troop == battle::UnitTroop::Spearman ? TEXT("Копейщики") : TEXT("Пехота");
}

class FCombatAudit
{
public:
    explicit FCombatAudit(const battle::World& World)
    {
        TSet<uint64> Staff;
        for (const auto& Officer : World.officers)
            Staff.Add(Officer.unit.value);

        TMap<FString, int32> IndexByProfile;
        for (const auto& Unit : World.units)
        {
            if (!Unit.alive || !Unit.regiment || Staff.Contains(Unit.id.value))
                continue;
            const int32 Team = battle::UnitTeam(World, Unit);
            auto Profile = ProfileName(Unit);
            const auto Key = FString::Printf(TEXT("%d:%s"), Team, *Profile);
            const int32* Found = IndexByProfile.Find(Key);
            const int32 Row = Found ? *Found : Troops.Add(FTroopAudit{MoveTemp(Profile), Team});
            if (!Found)
                IndexByProfile.Add(Key, Row);
            RowByUnit.Add(Unit.id.value, Row);
            ++Troops[Row].Deployed;
        }
    }

    void Observe(const battle::World& World)
    {
        for (const auto& Event : World.events)
        {
            if (Event.kind != battle::EventKind::AttackLaunched && Event.kind != battle::EventKind::Hit &&
                Event.kind != battle::EventKind::Died)
                continue;
            const auto* Index = RowByUnit.Find(Event.source.value);
            const auto* Target = Index ? battle::FindUnit(World, Event.target) : nullptr;
            if (!Target)
                continue;
            auto& Row = Troops[*Index];
            if (battle::UnitTeam(World, *Target) == Row.Team)
                continue;
            if (Event.kind == battle::EventKind::AttackLaunched)
            {
                ++Row.Attacks;
                if (!FirstAttack)
                    FirstAttack = Event.tick;
            }
            else if (Event.kind == battle::EventKind::Hit)
            {
                ++Row.Hits;
                Row.Damage += Event.amount;
            }
            else
            {
                ++Row.Kills;
            }
        }
    }

    TArray<FTroopAudit> Troops;
    TOptional<battle::Tick> FirstAttack;

private:
    TMap<uint64, int32> RowByUnit;
};

const TCHAR* StatusText(ETroopAuditStatus Status)
{
    switch (Status)
    {
    case ETroopAuditStatus::NoAttacks: return TEXT("Нет атак");
    case ETroopAuditStatus::NoHits: return TEXT("Нет попаданий");
    case ETroopAuditStatus::NoDamage: return TEXT("Попадания без урона");
    case ETroopAuditStatus::NoKills: return TEXT("Урон без убийств");
    case ETroopAuditStatus::Kills: return TEXT("Есть убийства");
    }
    return TEXT("");
}

FString SideName(int32 Team)
{
    if (Team == 0)
        return TEXT("Синие");
    if (Team == 1)
        return TEXT("Оранжевые");
    return FString::Printf(TEXT("Сторона %d"), Team);
}

FString ReportCell(const FString& Value)
{
    return Value.Replace(TEXT("|"), TEXT("\\|")).Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" "));
}

struct FAuditSummary
{
    int32 Ran = 0;
    int32 Incomplete = 0;
    int32 WithoutAttacks = 0;
    int32 ZeroKills = 0;
    int32 Profiles = 0;
    int32 Skipped = 0;
};

void AuditMission(FAutomationTestBase& Test, const FMissionPreset& Preset, int32 Seed, int32 MaxSeconds,
    bool bStrict, FAuditSummary& Summary, FString& Report)
{
    const auto Opening = MakeMissionOpening(Preset.Roster, Preset.Terrain, Preset.Field, Preset.FigureTarget);
    const auto Scenario = BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart);
    auto Created = battle::CreateWorld(Scenario, uint64(Seed));
    if (!Created)
    {
        Test.AddError(Preset.Name + TEXT(" did not create a tactical battle."));
        Report += TEXT("Ошибка создания тактического боя.\n\n");
        return;
    }

    auto& World = Created.value;
    FCombatAudit Audit(World);
    const auto Navigation = MakeMissionNavigation(Scenario.obstacles, Preset.Field);
    FRegimentMissionProgress Progress(Preset, World);
    constexpr auto StepsPerSecond = FRegimentBattlePlayback::StepsPerSecond;
    const auto StepLimit = battle::Tick(MaxSeconds) * StepsPerSecond;
    bool bStepFailed = false;
    while (World.outcome == battle::BattleOutcome::Running && World.tick < StepLimit)
    {
        if (!battle::Step(World, {}, {}, Navigation))
        {
            Test.AddError(FString::Printf(TEXT("%s failed at battle tick %llu."), *Preset.Name, World.tick));
            bStepFailed = true;
            break;
        }
        Audit.Observe(World);
        Progress.Update(World);
    }
    ++Summary.Ran;
    if (World.outcome == battle::BattleOutcome::Running && !bStepFailed)
        ++Summary.Incomplete;
    if (!Audit.FirstAttack)
    {
        ++Summary.WithoutAttacks;
        Test.AddError(Preset.Name + TEXT(" finished its audit without any hostile attacks by combat troops."));
    }

    const auto Finish = [&]() -> FString
    {
        if (bStepFailed)
            return TEXT("Ошибка шага симуляции");
        if (World.outcome == battle::BattleOutcome::Running)
            return TEXT("Лимит проверки, бой продолжается");
        if (World.outcome == battle::BattleOutcome::Draw)
            return TEXT("Ничья");
        return TEXT("Победа · ") + SideName(World.winner_team);
    }();
    const auto FirstAttack = Audit.FirstAttack
        ? FString::Printf(TEXT("%.2f с"), double(*Audit.FirstAttack) / StepsPerSecond)
        : FString(TEXT("боевые части не атаковали"));
    Report += FString::Printf(TEXT("%s · %.1f с симуляции. Первая атака — %s.\n\n"),
        *Finish, double(World.tick) / StepsPerSecond, *FirstAttack);
    Report += TEXT("| Сторона | Вид войск | В строю на старте | Атаки | Попадания | Урон | Убийства | Результат |\n")
              TEXT("|---|---|---:|---:|---:|---:|---:|---|\n");

    Audit.Troops.Sort([](const FTroopAudit& A, const FTroopAudit& B)
    {
        return A.Team != B.Team ? A.Team < B.Team : A.Profile < B.Profile;
    });
    int32 ZeroKills = 0;
    for (const auto& Row : Audit.Troops)
    {
        Report += FString::Printf(TEXT("| %s | %s | %d | %d | %d | %.1f | %d | %s |\n"),
            *SideName(Row.Team), *ReportCell(Row.Profile), Row.Deployed, Row.Attacks, Row.Hits, Row.Damage,
            Row.Kills, StatusText(Row.Status()));
        ZeroKills += Row.Kills == 0;
    }
    Report += TEXT("\n");
    Summary.Profiles += Audit.Troops.Num();
    Summary.ZeroKills += ZeroKills;
    if (ZeroKills > 0)
    {
        auto Message = FString::Printf(TEXT("%s: %d of %d troop profiles scored zero enemy kills."),
            *Preset.Name, ZeroKills, Audit.Troops.Num());
        if (bStrict)
            Test.AddError(MoveTemp(Message));
        else
            Test.AddWarning(MoveTemp(Message));
    }
}

battle::Scenario AuditDuel(float Damage, float Accuracy)
{
    battle::Scenario Scenario;
    Scenario.rules.regiment_tactics = true;
    Scenario.rules.ranged_cohorts = 1;
    for (int32 Side = 0; Side < 2; ++Side)
    {
        battle::RegimentSpec Regiment;
        Regiment.id = {uint64(Side + 1)};
        Regiment.team = Side;
        Regiment.pose = {{Side ? 12.f : 0.f, 0.f, 0.f}, Side ? battle::Pi : 0.f};
        Regiment.formation.columns = 1;
        Scenario.regiments.push_back(Regiment);
        battle::UnitSpec Unit;
        Unit.id = {uint64(Side + 1)};
        Unit.regiment = Regiment.id;
        Unit.pose = Regiment.pose;
        Unit.name = "Audit profile";
        Unit.health = 20.f;
        if (Side == 0)
        {
            Unit.archer = true;
            Unit.platform = battle::BattlePlatform::FieldGun;
            Unit.ammunition = 1;
            Unit.weapon = {Damage, 96.f, 208, 14, 84.f, Accuracy};
        }
        Scenario.units.push_back(Unit);
    }
    return Scenario;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentCombatAuditAccountingTest, "RegimentBattle.CombatAudit.Accounting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegimentCombatAuditAccountingTest::RunTest(const FString&)
{
    const battle::FlatNavigationService Navigation;
    for (const auto [Damage, Accuracy, ExpectedHits, ExpectedKills, ExpectedStatus] : {
            std::tuple{26.f, 1.f, 1, 1, ETroopAuditStatus::Kills},
            std::tuple{9.3f, 1.f, 1, 0, ETroopAuditStatus::NoKills},
            std::tuple{0.f, 1.f, 1, 0, ETroopAuditStatus::NoDamage},
            std::tuple{26.f, 0.f, 0, 0, ETroopAuditStatus::NoHits}})
    {
        auto Battle = battle::CreateWorld(AuditDuel(Damage, Accuracy), 42);
        if (!TestTrue(TEXT("The artillery fixture starts"), bool(Battle)))
            return false;
        auto& World = Battle.value;
        FCombatAudit Audit(World);
        for (int32 Tick = 0; Tick < 20 && World.outcome == battle::BattleOutcome::Running; ++Tick)
        {
            if (!TestTrue(TEXT("The artillery fixture steps"), bool(battle::Step(World, {}, {}, Navigation))))
                return false;
            Audit.Observe(World);
        }
        if (!TestEqual(TEXT("The same profile is represented separately on both sides"), Audit.Troops.Num(), 2))
            return false;
        const auto* Gun = Audit.Troops.FindByPredicate([](const FTroopAudit& Row) { return Row.Team == 0; });
        const auto* Enemy = Audit.Troops.FindByPredicate([](const FTroopAudit& Row) { return Row.Team == 1; });
        if (!TestNotNull(TEXT("Blue gun is tracked"), Gun) || !TestNotNull(TEXT("Orange target is tracked"), Enemy))
            return false;
        TestEqual(TEXT("Gun fires its one round"), Gun->Attacks, 1);
        TestEqual(TEXT("Gun hits counted from resolved events"), Gun->Hits, ExpectedHits);
        TestEqual(TEXT("Only actual kills count"), Gun->Kills, ExpectedKills);
        TestTrue(TEXT("Gun is classified by its first missing result"), Gun->Status() == ExpectedStatus);
        TestTrue(TEXT("Inactive target has no attacks"), Enemy->Status() == ETroopAuditStatus::NoAttacks);

        // Friendly fire and other non-combat losses must never count as hostile kills.
        battle::BattleEvent FriendlyLoss;
        FriendlyLoss.kind = battle::EventKind::Died;
        FriendlyLoss.source = FriendlyLoss.target = {1};
        World.events = {FriendlyLoss};
        Audit.Observe(World);
        TestEqual(TEXT("Friendly loss leaves enemy kills unchanged"), Gun->Kills, ExpectedKills);
    }

    auto Scenario = AuditDuel(26.f, 1.f);
    battle::UnitSpec Officer;
    Officer.id = {3};
    Officer.regiment = {2};
    Officer.pose = Scenario.regiments[1].pose;
    Officer.name = "Staff";
    Scenario.units.push_back(Officer);
    Scenario.officers.push_back({{1}, Officer.id});
    const auto Battle = battle::CreateWorld(Scenario, 42);
    if (!TestTrue(TEXT("The fixture including staff starts"), bool(Battle)))
        return false;
    const FCombatAudit Audit(Battle.value);
    TestEqual(TEXT("Staff does not create a combat profile"), Audit.Troops.Num(), 2);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentCombatAuditScenariosTest, "RegimentCombatAudit.Scenarios",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegimentCombatAuditScenariosTest::RunTest(const FString&)
{
    const TCHAR* CommandLine = FCommandLine::Get();
    FString EraFilter;
    FString ReportPath = FPaths::ProjectSavedDir() / TEXT("Reports/CombatAudit.md");
    int32 Seed = 42;
    int32 MaxSeconds = 180;
    FParse::Value(CommandLine, TEXT("RegimentAuditEra="), EraFilter);
    FParse::Value(CommandLine, TEXT("RegimentAuditReport="), ReportPath);
    FParse::Value(CommandLine, TEXT("RegimentAuditSeed="), Seed);
    FParse::Value(CommandLine, TEXT("RegimentAuditMaxSeconds="), MaxSeconds);
    const bool bStrict = FParse::Param(CommandLine, TEXT("RegimentAuditStrict"));
    const bool bIncludeLarge = FParse::Param(CommandLine, TEXT("RegimentAuditIncludeLarge"));
    if (Seed < 0 || MaxSeconds < 1 || MaxSeconds > 600)
    {
        AddError(TEXT("Combat audit expects a non-negative seed and a time limit from 1 to 600 seconds."));
        return false;
    }

    FAuditSummary Summary;
    FString Sections;
    const FString PresetDirectory = RegimentMissionPresets::Directory();
    for (const auto& Module : RegimentGameModules::List())
    {
        if (!EraFilter.IsEmpty() && EraFilter != Module.Id)
            continue;
        if (!Module.bAvailable)
        {
            AddError(Module.Id + TEXT(" cannot be opened."));
            Sections += FString::Printf(TEXT("## %s\n\nМодуль не удалось открыть.\n\n"), *Module.Id);
            continue;
        }
        for (const auto& Item : RegimentGameModules::Presets(Module.Id, PresetDirectory))
        {
            if (!Item.bBuiltIn || Item.bSharedAcrossModules || Item.EraId != Module.Id)
                continue;
            Sections += FString::Printf(TEXT("## %s\n\n"), *Item.Name);
            FText Error;
            FMissionPreset Preset;
            if (!RegimentGameModules::Load(Module.Id, PresetDirectory, Item, Preset, Error))
            {
                AddError(Item.Name + TEXT(" cannot be loaded. ") + Error.ToString());
                Sections += TEXT("Ошибка загрузки сценария.\n\n");
                continue;
            }
            if (Preset.FigureTarget >= MissionMaxFigureTarget && !bIncludeLarge)
            {
                ++Summary.Skipped;
                Sections += TEXT("Нагрузочный сценарий. Для прогона добавьте `-IncludeLarge`.\n\n");
                continue;
            }
            AuditMission(*this, Preset, Seed, MaxSeconds, bStrict, Summary, Sections);
        }
    }
    if (Summary.Ran == 0)
    {
        AddError(TEXT("No authored battles were audited. Check the selected era and project content."));
        Sections += TEXT("Готовых боевых сценариев для выбранной эпохи не найдено.\n");
    }

    FString Report = FString::Printf(
        TEXT("# Проверка видов войск\n\n")
        TEXT("Область — %s. Seed — %d. Лимит — %d с на бой. Строгий режим — %s.\n\n")
        TEXT("Проверено сценариев — **%d**. Строк по сторонам и видам войск — **%d**, ")
        TEXT("из них без убийств — **%d**. Без атак — **%d** сценариев. ")
        TEXT("Достигли лимита — **%d**. Пропущено нагрузочных сценариев — **%d**.\n\n")
        TEXT("Прогон начинается с тактического развёртывания. Статистика считает действия боевых частей ")
        TEXT("против другой стороны. Убийство засчитывается источнику смертельного попадания.\n\n"),
        EraFilter.IsEmpty() ? TEXT("все эпохи") : *EraFilter, Seed, MaxSeconds, bStrict ? TEXT("да") : TEXT("нет"),
        Summary.Ran, Summary.Profiles, Summary.ZeroKills, Summary.WithoutAttacks, Summary.Incomplete,
        Summary.Skipped);
    Report += Sections;

    const auto Directory = FPaths::GetPath(ReportPath);
    if (!Directory.IsEmpty())
        IFileManager::Get().MakeDirectory(*Directory, true);
    if (!FFileHelper::SaveStringToFile(Report, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        AddError(TEXT("Unable to write combat audit report to ") + ReportPath);
    else
        AddInfo(TEXT("Combat audit report written to ") + ReportPath);
    return !HasAnyErrors();
}
#endif
