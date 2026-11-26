#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentEraCatalog.h"
#include "RegimentMissionScenario.h"
#include "battle/Simulation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentAncientBattleTest, "RegimentBattle.Eras.AncientPlatformBattle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentAncientBattleTest::RunTest(const FString& Parameters)
{
    using battle::BattlePlatform;
    const auto* Era = RegimentEraCatalog::Find(TEXT("ancient"));
    if (!TestNotNull(TEXT("Ancient era is installed"), Era))
        return false;

    FMissionPreset Preset;
    RegimentMissionPresets::ApplyEra(Preset, *Era);
    Preset.Era.BlueFaction = TEXT("persia");
    Preset.Era.OrangeFaction = TEXT("carthage");
    Preset.Terrain = battle::MarchTerrain::OpenField;
    Preset.bDirectDeployment = true;
    Preset.Field = {160, 120};

    for (auto& Entry : Preset.Roster)
        Entry.bEnabled = false;
    constexpr BattlePlatform Platforms[] = {BattlePlatform::IndianElephant, BattlePlatform::ScythedChariot,
        BattlePlatform::Catapult, BattlePlatform::Ballista, BattlePlatform::AfricanElephant};
    constexpr int32 Slots[] = {0, 1, 2, 3, MissionRegimentsPerTeam};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Platforms); ++Index)
    {
        auto& Entry = Preset.Roster[Slots[Index]];
        Entry.bEnabled = true;
        Entry.Platform = Platforms[Index];
        Entry.PlatformCount = 2;
        Entry.StartX = Index == 4 ? 20.f : -20.f;
        Entry.StartY = -24.f + Index * 14.f;
    }
    constexpr int32 InfantrySlot = MissionRegimentsPerTeam + 1;
    Preset.Roster[InfantrySlot] = FMissionRosterEntry(15, 3, 6);
    Preset.Roster[InfantrySlot].StartX = 18.f;
    Preset.Roster[InfantrySlot].StartY = 2.f;

    const int32 BaseCount = MissionFigureCount(Preset.Roster, true);
    for (const int32 FigureTarget : {0, BaseCount + 30})
    {
        Preset.FigureTarget = FigureTarget;
        const auto Formation = MakeMissionOpening(Preset.Roster, Preset.Terrain, Preset.Field, FigureTarget);
        const auto Scenario = BuildPresetBattle(Preset, Formation, EMissionDeployment::AuthoredStart);
        if (!TestEqual(TEXT("Scenario has exactly the requested head count"),
                int32(Scenario.units.size()), FigureTarget ? FigureTarget : BaseCount))
            continue;

        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Platforms); ++Index)
        {
            const auto RegimentId = battle::RegimentId{uint64(Slots[Index] + 1)};
            int32 EngineCount = 0;
            int32 StaffCount = 0;
            for (const auto& Unit : Scenario.units)
            {
                if (Unit.regiment != RegimentId)
                    continue;
                if (Unit.platform == BattlePlatform::None)
                {
                    ++StaffCount;
                    TestFalse(TEXT("Command staff in platform detachments keep ordinary weapons"), Unit.archer);
                    continue;
                }
                ++EngineCount;
                TestTrue(TEXT("The chosen specialist reaches BattleCore"), Unit.platform == Platforms[Index]);
                if (Unit.platform == BattlePlatform::Catapult || Unit.platform == BattlePlatform::Ballista)
                {
                    TestTrue(TEXT("Artillery uses the projectile path"), Unit.archer);
                    const bool bCatapult = Unit.platform == BattlePlatform::Catapult;
                    TestEqual(TEXT("The engine receives rulebook ammunition"), Unit.ammunition, bCatapult ? 12u : 14u);
                    TestEqual(TEXT("The engine receives the correct range"), Unit.weapon.range, bCatapult ? 40.f : 30.f);
                }
            }
            TestEqual(TEXT("Scaling preserves each specialist count"), EngineCount, 2);
            TestEqual(TEXT("Platform detachments retain the regiment's command staff"), StaffCount, MissionCommandStaffCount);
        }

        const auto World = battle::CreateWorld(Scenario, 42);
        TestTrue(TEXT("BattleCore accepts all five platforms in the same scenario"), bool(World));
    }

    constexpr FGuid ExerciseId(0x6C326E44, 0xB6484ABB, 0x893D83EE, 0xDB4F9D6F);
    if (!TestTrue(TEXT("The Ancient specialist exercise loads"), RegimentMissionPresets::LoadBuiltIn(ExerciseId, Preset)))
        return false;
    const auto Opening = MakeMissionOpening(Preset.Roster, Preset.Terrain, Preset.Field);
    const auto Scenario = BuildPresetBattle(Preset, Opening, EMissionDeployment::AuthoredStart);
    const auto Navigation = MakeMissionNavigation(Scenario.obstacles, Preset.Field);
    struct FShot
    {
        int32 PlatformIndex;
        battle::UnitId Target;
        battle::Tick DueTick;
    };
    struct FStats
    {
        int32 Launches = 0;
        int32 Hits = 0;
        int32 Kills = 0;
        float Damage = 0.f;
    };
    int32 HitsByPlatform[2]{};
    constexpr int32 ExerciseTicks = 720;
    for (const uint64 Seed : {42ull, 73ull, 101ull})
    {
        auto World = battle::CreateWorld(Scenario, Seed);
        if (!TestTrue(TEXT("The exercise starts a BattleCore battle"), bool(World)))
            return false;

        TMap<uint64, FShot> Flights;
        FStats Stats[2];
        for (int32 Tick = 0; Tick < ExerciseTicks &&
             World.value.outcome == battle::BattleOutcome::Running; ++Tick)
        {
            const auto Stepped = battle::Step(World.value, {}, {}, Navigation);
            if (!Stepped)
            {
                AddError(TEXT("The exercise failed to advance."));
                break;
            }
            for (const auto& Event : Stepped.value.events)
            {
                if (Event.kind == battle::EventKind::AttackLaunched && Event.projectile)
                {
                    const auto* Shooter = battle::FindUnit(World.value, Event.source);
                    if (!Shooter || (Shooter->platform != BattlePlatform::Catapult &&
                                     Shooter->platform != BattlePlatform::Ballista))
                        continue;
                    const bool bCatapult = Shooter->platform == BattlePlatform::Catapult;
                    const int32 Index = bCatapult ? 0 : 1;
                    TestEqual(TEXT("Catapult stones arc and ballista bolts fly straight"), Event.high_arc, bCatapult);
                    TestTrue(TEXT("Siege projectile has a flight time"), Event.due_tick > Event.tick);
                    Flights.Add(Event.id.value, FShot{Index, Event.target, Event.due_tick});
                    ++Stats[Index].Launches;
                }
                else if (const auto* Shot = Flights.Find(Event.cause.value))
                {
                    auto& Weapon = Stats[Shot->PlatformIndex];
                    if (Event.kind == battle::EventKind::Hit && Event.amount > 0.f)
                    {
                        TestTrue(TEXT("Projectile damage arrives at the recorded flight tick and target"),
                            Event.tick == Shot->DueTick && Event.target == Shot->Target);
                        ++Weapon.Hits;
                        Weapon.Damage += Event.amount;
                    }
                    else if (Event.kind == battle::EventKind::Died)
                    {
                        ++Weapon.Kills;
                    }
                }
            }
        }
        TestTrue(TEXT("The exercise catapult launches a projectile"), Stats[0].Launches > 0);
        TestTrue(TEXT("The exercise ballista launches a projectile"), Stats[1].Launches > 0);
        for (int32 Index = 0; Index < 2; ++Index)
        {
            HitsByPlatform[Index] += Stats[Index].Hits;
            AddInfo(FString::Printf(TEXT("Seed %llu %s — %d launched / %d damaging hits / %.2f damage / %d kills"),
                Seed, Index == 0 ? TEXT("catapult") : TEXT("ballista"),
                Stats[Index].Launches, Stats[Index].Hits, Stats[Index].Damage, Stats[Index].Kills));
        }
    }
    TestTrue(TEXT("The exercise catapult deals real damage"), HitsByPlatform[0] > 0);
    TestTrue(TEXT("The exercise ballista deals real damage"), HitsByPlatform[1] > 0);
    return !HasAnyErrors();
}
#endif
