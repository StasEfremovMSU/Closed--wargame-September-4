#include "RegimentEraCatalog.h"
#include "RegimentFigureEquipment.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

namespace
{
const TCHAR* const ExampleModule = TEXT(R"json(
{
  "schema_version": 1,
  "id": "test_era",
  "name": "Test era",
  "version": 3,
  "rules": {
    "id": "formation_combat",
    "local_melee": true,
    "regiment_tactics": true,
    "risky_volley": false,
    "ranged_cohorts": 7,
    "panic_fraction": 0.45
  },
  "archetypes": {
    "levy": {"name":"Levy","health":2,"speed":3,"armour":0.25,"damage":1,"range":2,
      "cooldown_ticks":20,"windup_ticks":2,"projectile_speed_mps":20,"base_accuracy":0.75},
    "bow": {"name":"Bow","health":1,"speed":2,"armour":0,"damage":2,"range":30,
      "cooldown_ticks":24,"windup_ticks":3,"projectile_speed_mps":40,"base_accuracy":0.6}
  },
  "factions": [
    {"id":"blue_army","name":"Blue army","shield":"round","heraldry":"0000FF",
      "troops":{"swords":"levy","archers":"bow","spearmen":"levy","mounted":"levy"}},
    {"id":"orange_army","name":"Orange army","shield":"kite","heraldry":"FF8000",
      "troops":{"swords":"levy","archers":"bow","spearmen":"levy","mounted":"levy"}}
  ],
  "mission": {
    "id":"3ce18612-671d-4ff8-9429-325d1f23060d", "name":"Example battle",
    "briefing":"Defeat the opposing army.", "blue_faction":"blue_army", "orange_faction":"orange_army",
    "blue":[{"swords":32,"archers":5,"spearmen":8,"mounted":0,"x":-30,"y":0,"group":2}],
    "orange":[{"swords":0,"archers":0,"spearmen":0,"mounted":26,"x":30,"y":0,"group":1}]
  }
}
)json");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentEraCatalogParseTest, "RegimentBattle.Eras.ParseModule",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentEraCatalogParseTest::RunTest(const FString& Parameters)
{
    FRegimentEraDefinition Module;
    FText Error;
    if (!TestTrue(TEXT("Accepts a complete module"), RegimentEraCatalog::Parse(ExampleModule, Module, Error)))
    {
        AddError(Error.ToString());
        return false;
    }
    TestEqual(TEXT("Keeps the module identity"), Module.Id, FString(TEXT("test_era")));
    TestEqual(TEXT("Keeps the module version"), Module.Version, 3);
    TestEqual(TEXT("Keeps the rules profile"), Module.ProfileId, FString(TEXT("formation_combat")));
    TestEqual(TEXT("Keeps the combat cohort count"), Module.Rules.ranged_cohorts, 7u);
    TestTrue(TEXT("Enables regiment tactics"), Module.Rules.regiment_tactics);
    if (const auto* Faction = RegimentEraCatalog::FindFaction(Module, TEXT("orange_army")))
    {
        TestEqual(TEXT("Assigns the kite shield"), Faction->Shield, ERegimentEraShield::Kite);
        TestEqual(TEXT("Parses the heraldry"), Faction->Heraldry.ToFColorSRGB(), FColor(255, 128, 0));
        TestEqual(TEXT("Resolves the archer weapon"), Faction->Troops[int32(ERegimentEraRole::Archers)].Weapon.range, 30.f);
    }
    else
    {
        AddError(TEXT("The orange faction could not be resolved"));
    }
    TestEqual(TEXT("Keeps the start ordinate"), Module.Roster[0].StartX.value_or(0.f), -30.f);
    TestEqual(TEXT("Keeps the group"), Module.Roster[0].Group, 2);
    TestTrue(TEXT("Activates infantry"), Module.Roster[0].bEnabled && !Module.Roster[0].bCavalry);
    TestTrue(TEXT("Activates mounted regiment"), Module.Roster[6].bEnabled && Module.Roster[6].bCavalry);
    TestFalse(TEXT("Disables omitted blue regiment"), Module.Roster[1].bEnabled);
    TestFalse(TEXT("Disables omitted orange regiment"), Module.Roster[7].bEnabled);
    if (const auto* Faction = RegimentEraCatalog::FindFaction(Module, TEXT("orange_army")))
    {
        const auto* LegacyMount = RegimentEraCatalog::MountedTroop(*Faction, ERegimentMountClass::FactionDefault);
        const auto* LegacyBow = RegimentEraCatalog::RangedTroop(*Faction, ERegimentRangedClass::FactionDefault);
        TestTrue(TEXT("Unclassified plain mounted profiles default to light"),
            LegacyMount && LegacyMount->Cavalry == battle::CavalryClass::Light);
        TestTrue(TEXT("Legacy ranged profiles remain unlimited"),
            LegacyBow && LegacyBow->RangedWeapon == battle::RangedWeapon::Bow && LegacyBow->Ammunition == 0);
        TestNull(TEXT("Unavailable profiles cannot be selected"),
            RegimentEraCatalog::RangedTroop(*Faction, ERegimentRangedClass::Crossbow));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentEraCatalogRejectTest, "RegimentBattle.Eras.RejectInvalidModule",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentEraCatalogRejectTest::RunTest(const FString& Parameters)
{
    const struct
    {
        const TCHAR* Original;
        const TCHAR* Replacement;
        const TCHAR* ErrorField;
    } InvalidCases[] =
    {
        {TEXT("\"schema_version\": 1"), TEXT("\"schema_version\": 2"), TEXT("schema_version")},
        {TEXT("\"version\": 3"), TEXT("\"version\": 0"), TEXT("version")},
        {TEXT("\"test_era\""), TEXT("\"../test_era\""), TEXT("id")},
        {TEXT("\"archers\":\"bow\""), TEXT("\"archers\":\"missing\""), TEXT("archers")},
        {TEXT("\"health\":2"), TEXT("\"health\":0"), TEXT("health")},
        {TEXT("\"health\":2"), TEXT("\"health\":2,\"cavalry_class\":\"armoured\""), TEXT("cavalry_class")},
        {TEXT("\"health\":2"), TEXT("\"health\":2,\"ammunition\":8"), TEXT("ammunition")},
        {TEXT("\"health\":1"), TEXT("\"health\":1,\"ranged_weapon\":\"bow\",\"ammunition\":0"), TEXT("ammunition")},
        {TEXT("\"heraldry\":\"0000FF\""),
            TEXT("\"heraldry\":\"0000FF\",\"mounted_variants\":{\"heavy\":\"levy\"}"), TEXT("heavy")},
        {TEXT("\"id\":\"orange_army\""), TEXT("\"id\":\"blue_army\""), TEXT("id")},
        {TEXT("\"FF8000\""), TEXT("\"FF8Q00\""), TEXT("heraldry")},
        {TEXT("\"blue_faction\":\"blue_army\""), TEXT("\"blue_faction\":\"missing\""), TEXT("blue_faction")},
        {TEXT("\"swords\":32"), TEXT("\"swords\":78"), TEXT("swords")},
        {TEXT("\"swords\":32"), TEXT("\"swords\":32.5"), TEXT("swords")},
        {TEXT("\"swords\":32"), TEXT("\"swords\":32,\"swords\":33"), TEXT("swords")},
        {TEXT("\"mounted\":0"), TEXT("\"mounted\":12"), TEXT("blue")},
        {TEXT("\"x\":-30"), TEXT("\"x\":-80"), TEXT("x")},
        {TEXT("\"group\":2"), TEXT("\"group\":4"), TEXT("group")},
    };
    for (const auto& Invalid : InvalidCases)
    {
        const FString Json = FString(ExampleModule).Replace(Invalid.Original, Invalid.Replacement);
        FRegimentEraDefinition Module;
        Module.Id = TEXT("existing");
        FText Error;
        TestFalse(Invalid.Replacement, RegimentEraCatalog::Parse(Json, Module, Error));
        TestTrue(TEXT("Identifies the invalid field"), Error.ToString().Contains(Invalid.ErrorField));
        TestEqual(TEXT("Preserves existing state after rejection"), Module.Id, FString(TEXT("existing")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentAncientPlatformCatalogTest, "RegimentBattle.Eras.AncientPlatforms",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentAncientPlatformCatalogTest::RunTest(const FString&)
{
    using enum battle::BattlePlatform;
    const auto* Era = RegimentEraCatalog::Find(TEXT("ancient"));
    if (!TestNotNull(TEXT("Ancient era loads"), Era))
        return false;

    const auto* Rome = RegimentEraCatalog::FindFaction(*Era, TEXT("rome"));
    const auto* Persia = RegimentEraCatalog::FindFaction(*Era, TEXT("persia"));
    const auto* Carthage = RegimentEraCatalog::FindFaction(*Era, TEXT("carthage"));
    if (!TestNotNull(TEXT("Roman roster"), Rome) || !TestNotNull(TEXT("Persian roster"), Persia) ||
        !TestNotNull(TEXT("Carthaginian roster"), Carthage))
        return false;

    TestNotNull(TEXT("Persia can field Indian elephants"), RegimentEraCatalog::PlatformTroop(*Persia, IndianElephant));
    TestNotNull(TEXT("Persia can field scythed chariots"), RegimentEraCatalog::PlatformTroop(*Persia, ScythedChariot));
    TestNotNull(TEXT("Carthage can field African elephants"), RegimentEraCatalog::PlatformTroop(*Carthage, AfricanElephant));
    TestNull(TEXT("Rome cannot field Persian chariots"), RegimentEraCatalog::PlatformTroop(*Rome, ScythedChariot));

    if (const auto* CatapultTroop = RegimentEraCatalog::PlatformTroop(*Rome, Catapult))
    {
        TestEqual(TEXT("Catapult has limited ammunition"), CatapultTroop->Ammunition, 12u);
        TestEqual(TEXT("Catapult has a 40 m maximum range"), CatapultTroop->Weapon.range, 40.f);
        TestTrue(TEXT("Catapult can be moved slowly"), CatapultTroop->Speed > 0.f && CatapultTroop->Speed <= 1.f);
    }
    else
        AddError(TEXT("Rome must be able to field catapults"));

    if (const auto* BallistaTroop = RegimentEraCatalog::PlatformTroop(*Rome, Ballista))
    {
        TestEqual(TEXT("Ballista has limited ammunition"), BallistaTroop->Ammunition, 14u);
        TestEqual(TEXT("Ballista has a 30 m maximum range"), BallistaTroop->Weapon.range, 30.f);
        TestEqual(TEXT("Ballista is stationary"), BallistaTroop->Speed, 0.f);
    }
    else
        AddError(TEXT("Rome must be able to field ballistae"));

    FString Invalid(ExampleModule);
    Invalid.ReplaceInline(TEXT("\"heraldry\":\"0000FF\""),
        TEXT("\"heraldry\":\"0000FF\",\"platforms\":{\"catapult\":\"levy\"}"));
    FRegimentEraDefinition Parsed;
    FText Error;
    TestFalse(TEXT("An ordinary soldier cannot be declared a catapult"), RegimentEraCatalog::Parse(Invalid, Parsed, Error));
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentFigureEquipmentTest, "RegimentBattle.Presentation.BayonetEquipment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentFigureEquipmentTest::RunTest(const FString& Parameters)
{
    const auto* Napoleon = RegimentEraCatalog::Find(TEXT("napoleonic"));
    const auto* Northern = RegimentEraCatalog::Find(TEXT("great_northern_war"));
    if (!TestNotNull(TEXT("Napoleonic catalog"), Napoleon) || !TestNotNull(TEXT("Northern War catalog"), Northern))
        return false;
    const auto* French = RegimentEraCatalog::FindFaction(*Napoleon, TEXT("france"));
    const auto* British = RegimentEraCatalog::FindFaction(*Napoleon, TEXT("britain"));
    const auto* Russians = RegimentEraCatalog::FindFaction(*Northern, TEXT("russia"));
    const auto* Swedes = RegimentEraCatalog::FindFaction(*Northern, TEXT("sweden"));
    if (!TestNotNull(TEXT("French troops"), French) || !TestNotNull(TEXT("British troops"), British)
        || !TestNotNull(TEXT("Russian troops"), Russians) || !TestNotNull(TEXT("Swedish troops"), Swedes))
        return false;

    battle::UnitSpec Unit;
    Unit.troop = battle::UnitTroop::Spearman;
    TestTrue(TEXT("French bayonet line carries a musket"), RegimentShowsBayonetMusket(Unit, French));
    TestTrue(TEXT("British bayonet line carries a musket"), RegimentShowsBayonetMusket(Unit, British));
    TestTrue(TEXT("Russian bayonet infantry carries a musket"), RegimentShowsBayonetMusket(Unit, Russians));
    TestFalse(TEXT("Swedish pikemen keep their pikes"), RegimentShowsBayonetMusket(Unit, Swedes));
    TestFalse(TEXT("Legacy unprofiled spearmen keep their spears"), RegimentShowsBayonetMusket(Unit, nullptr));
    Unit.troop = battle::UnitTroop::Infantry;
    TestTrue(TEXT("Napoleonic grenadiers show their bayonet muskets"), RegimentShowsBayonetMusket(Unit, French));
    Unit.archer = true;
    Unit.ranged_weapon = battle::RangedWeapon::Musket;
    TestTrue(TEXT("A ranged musket still shows a bayonet musket"), RegimentShowsBayonetMusket(Unit, French));
    Unit.ranged_weapon = battle::RangedWeapon::Bow;
    TestFalse(TEXT("An archer keeps the actual ranged weapon"), RegimentShowsBayonetMusket(Unit, French));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentNapoleonicEquipmentTest, "RegimentBattle.Presentation.NapoleonicEquipment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentNapoleonicEquipmentTest::RunTest(const FString&)
{
    const auto* Napoleon = RegimentEraCatalog::Find(TEXT("napoleonic"));
    if (!TestNotNull(TEXT("Napoleonic catalog loads"), Napoleon))
        return false;
    TestEqual(TEXT("All nine factions are present"), Napoleon->Factions.Num(), 9);
    for (const auto& Faction : Napoleon->Factions)
    {
        battle::UnitSpec Infantry;
        for (const auto Role : {ERegimentEraRole::Swords, ERegimentEraRole::Spearmen, ERegimentEraRole::Archers})
        {
            Infantry.archer = Role == ERegimentEraRole::Archers;
            Infantry.troop = Role == ERegimentEraRole::Spearmen ? battle::UnitTroop::Spearman : battle::UnitTroop::Infantry;
            Infantry.ranged_weapon = Faction.Troops[int32(Role)].RangedWeapon;
            const bool bMusket = RegimentShowsBayonetMusket(Infantry, &Faction);
            TestTrue(Faction.Id + TEXT(" foot shows a musket"), bMusket);
            TestFalse(Faction.Id + TEXT(" foot has no polearm"), RegimentShowsPolearm(Infantry, bMusket, nullptr));
        }
        Infantry.troop = battle::UnitTroop::Mounted;
        Infantry.archer = false;
        for (const auto Class : {ERegimentMountClass::FactionDefault, ERegimentMountClass::Light, ERegimentMountClass::Heavy})
        {
            const auto* Profile = RegimentEraCatalog::MountedTroop(Faction, Class);
            if (!TestNotNull(Faction.Id + TEXT(" cavalry profile exists"), Profile))
                continue;
            const bool bLancer = (Faction.Id == TEXT("poland") && Class != ERegimentMountClass::Heavy) ||
                (Faction.Id == TEXT("russia") && Class == ERegimentMountClass::Light);
            TestEqual(Profile->Name + TEXT(" declares the weapon for the low-detail figure"),
                Profile->MountedWeapon, bLancer ? ERegimentMountedWeapon::Lance : ERegimentMountedWeapon::Blade);
            TestEqual(Profile->Name + TEXT(" uses the intended mounted weapon"),
                RegimentShowsPolearm(Infantry, false, Profile), bLancer);
        }
    }
    return true;
}

#endif
