#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentAncientFigures.h"
#include "RegimentEraCatalog.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "StaticMeshResources.h"
THIRD_PARTY_INCLUDES_START
#include "battle/Scenario.h"
THIRD_PARTY_INCLUDES_END

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentAncientPlatformFigureTest, "RegimentBattle.Presentation.AncientPlatformFigures",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentAncientPlatformFigureTest::RunTest(const FString& Parameters)
{
    using battle::BattlePlatform;
    using F = ERegimentAncientFeature;
    const auto Indian = BuildRegimentAncientPlatform(BattlePlatform::IndianElephant);
    const auto African = BuildRegimentAncientPlatform(BattlePlatform::AfricanElephant);
    for (const auto* Elephant : {&Indian, &African})
    {
        TestEqual(TEXT("One driver sits forward of the tower"), Elephant->Count(F::Driver), 1);
        TestEqual(TEXT("Two soldiers are visible in each tower"), Elephant->Count(F::TowerCrew), 2);
        TestEqual(TEXT("Elephant has ears and tusks on both sides"), Elephant->Count(F::Ear) + Elephant->Count(F::Tusk), 4);
        TestTrue(TEXT("Both soldiers have room above the elephant body"), Elephant->Bounds.Max.Z > 400.0);
    }
    TestTrue(TEXT("African elephant is larger"), African.Bounds.GetSize().X > Indian.Bounds.GetSize().X + 40.0);
    TestTrue(TEXT("African ears extend the silhouette"), African.Bounds.GetSize().Y > Indian.Bounds.GetSize().Y + 25.0);

    const auto Chariot = BuildRegimentAncientPlatform(BattlePlatform::ScythedChariot);
    TestEqual(TEXT("Four horses form the team"), Chariot.Count(F::Horse), 4);
    TestEqual(TEXT("One blade projects beyond each wheel"), Chariot.Count(F::Scythe), 2);
    TestEqual(TEXT("Driver and guard are visible"), Chariot.Count(F::Driver) + Chariot.Count(F::EngineCrew), 2);
    TestTrue(TEXT("The chariot fits within the formation cell"), Chariot.Bounds.GetSize().X <= 500.0);
    for (double Side : {-1.0, 1.0})
    {
        const FVector Aperture(-112, Side * 53, 105);
        TestFalse(TEXT("The space between the rails and posts is open"),
            Chariot.Parts.ContainsByPredicate([&](const FRegimentAncientPart& Part)
            {
                return Part.Transform.InverseTransformPosition(Aperture).GetAbsMax() <= .5;
            }));
    }

    const auto Catapult = BuildRegimentAncientPlatform(BattlePlatform::Catapult);
    const auto Ballista = BuildRegimentAncientPlatform(BattlePlatform::Ballista);
    TestEqual(TEXT("Catapult has a raised throwing arm"), Catapult.Count(F::ThrowingArm), 1);
    TestEqual(TEXT("Ballista has symmetric arms across the throwing rail"), Ballista.Count(F::BowArm), 2);
    TestEqual(TEXT("Catapult crew has two visible soldiers"), Catapult.Count(F::EngineCrew), 2);
    TestEqual(TEXT("Ballista crew has two visible soldiers"), Ballista.Count(F::EngineCrew), 2);
    TestTrue(TEXT("Catapult silhouette stands higher than the ballista"), Catapult.Bounds.Max.Z > Ballista.Bounds.Max.Z + 20.0);
    TestTrue(TEXT("Ordinary units use their existing models"), BuildRegimentAncientPlatform(BattlePlatform::None).Parts.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentAncientMeshBakeTest, "RegimentBattle.Presentation.AncientPlatformMeshBake",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentAncientMeshBakeTest::RunTest(const FString& Parameters)
{
    using battle::BattlePlatform;
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (!TestNotNull(TEXT("The platform painting material is available"), Material))
        return false;
    for (const auto Platform : {BattlePlatform::IndianElephant, BattlePlatform::AfricanElephant,
        BattlePlatform::ScythedChariot, BattlePlatform::Catapult, BattlePlatform::Ballista, BattlePlatform::FieldGun})
    {
        const auto Figure = BuildRegimentAncientPlatform(Platform);
        int32 PaintSlot = INDEX_NONE;
        const auto* Mesh = MakeRegimentAncientMesh(*GetTransientPackage(), Figure, Material, PaintSlot);
        if (!TestNotNull(TEXT("The platform bakes into a transient static mesh"), Mesh))
            continue;
        TestTrue(TEXT("Faction cloth has a valid material slot"), Mesh->GetStaticMaterials().IsValidIndex(PaintSlot));
        const auto* RenderData = Mesh->GetRenderData();
        const bool bHasLod = RenderData && RenderData->LODResources.IsValidIndex(0);
        TestTrue(TEXT("The runtime mesh has a renderable LOD"), bHasLod);
        if (bHasLod)
        {
            const auto& Lod = RenderData->LODResources[0];
            TestTrue(TEXT("Faction paint is used by a nonempty render section"),
                Lod.Sections.ContainsByPredicate([PaintSlot](const FStaticMeshSection& Section)
                {
                    return Section.NumTriangles > 0 && Section.MaterialIndex == PaintSlot;
                }));
        }
        TestTrue(TEXT("The culling radius surrounds a meter-scale figure"), Mesh->GetBounds().SphereRadius > 100);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentAncientStyleTest, "RegimentBattle.Presentation.AncientFactionStyles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentAncientStyleTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Rome uses a tall scutum"), RegimentAncientShieldStyle(TEXT("rome")) == ERegimentAncientShield::Scutum);
    TestTrue(TEXT("Persia uses a tall wicker shield"), RegimentAncientShieldStyle(TEXT("persia")) == ERegimentAncientShield::Wicker);
    TestTrue(TEXT("Gauls use an oval shield"), RegimentAncientShieldStyle(TEXT("gauls")) == ERegimentAncientShield::Oval);
    TestTrue(TEXT("Sparta uses an aspis"), RegimentAncientShieldStyle(TEXT("sparta")) == ERegimentAncientShield::Aspis);
    TestTrue(TEXT("Carthage uses its own round design"), RegimentAncientShieldStyle(TEXT("carthage")) == ERegimentAncientShield::Carthaginian);
    TestTrue(TEXT("Macedonians have a distinct pike silhouette"), RegimentAncientPikeLength(TEXT("macedon")) > 300.0);
    TestTrue(TEXT("Ordinary spears keep their baseline length"), RegimentAncientPikeLength(TEXT("rome")) == 242.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentAdditionalEraStyleTest, "RegimentBattle.Presentation.EraFigureStyles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentAdditionalEraStyleTest::RunTest(const FString&)
{
    for (int32 Index = 1; Index < int32(ERegimentEraVisual::Count); ++Index)
    {
        const auto Figure = BuildRegimentEraHeadgear(ERegimentEraVisual(Index));
        TestTrue(TEXT("Every era style has an authored silhouette"), Figure.Parts.Num() >= 3 && Figure.Bounds.IsValid);
    }
    TestTrue(TEXT("A giant is taller than a regular fighter"),
        RegimentEraFigureScale(ERegimentEraVisual::Giant) > RegimentEraFigureScale(ERegimentEraVisual::Classic));
    TestTrue(TEXT("A dwarf is shorter than a regular fighter"),
        RegimentEraFigureScale(ERegimentEraVisual::Dwarven) < RegimentEraFigureScale(ERegimentEraVisual::Classic));
    const auto Gun = BuildRegimentAncientPlatform(battle::BattlePlatform::FieldGun);
    TestEqual(TEXT("The field gun is an original mesh with a visible crew"),
        Gun.Count(ERegimentAncientFeature::EngineCrew), 2);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentHistoricalFigureTest, "RegimentBattle.Presentation.HistoricalFigureProfiles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentHistoricalFigureTest::RunTest(const FString&)
{
    using S = ERegimentHistoricalFigure;
    using F = ERegimentAncientFeature;
    using W = battle::RangedWeapon;
    TestTrue(TEXT("Roman infantry profiles keep different shield silhouettes"),
        RegimentHistoricalShield(S::Legionary) == ERegimentAncientShield::Scutum &&
        RegimentHistoricalShield(S::Auxiliary) == ERegimentAncientShield::Oval);
    TestTrue(TEXT("Roman cavalry carry a compact oval shield"),
        RegimentHistoricalShield(S::RomanCavalry) == ERegimentAncientShield::CavalryOval);
    TestTrue(TEXT("A Macedonian sarissa projects farther than a hoplite spear"),
        RegimentHistoricalSpearLength(S::Sarissa) > RegimentHistoricalSpearLength(S::Hoplite) + 100.0);
    TestFalse(TEXT("There is no shield on a Numidian light rider"), RegimentHistoricalShield(S::Numidian).IsSet());
    TestTrue(TEXT("Numidian tack consists of a light neck strap"),
        BuildRegimentHistoricalTack(S::Numidian).Parts.Num() == 1);
    TestTrue(TEXT("A Persian archer has a quiver and no wicker shield"),
        BuildRegimentHistoricalDress(S::PersianArcher).Count(F::Quiver) == 1 &&
        !RegimentHistoricalShield(S::PersianArcher).IsSet());
    TestTrue(TEXT("A sling has a visible pouch"), BuildRegimentLooseWeapon(W::Sling).Count(F::SlingPouch) == 1);
    TestTrue(TEXT("A javelin has a long straight shaft"), BuildRegimentLooseWeapon(W::Javelin).Bounds.GetSize().Z > 110.0);
    TestTrue(TEXT("The Old Guard bearskin stands higher than the Young Guard shako"),
        BuildRegimentHistoricalDress(S::OldGuard).Bounds.Max.Z >
        BuildRegimentHistoricalDress(S::YoungGuard).Bounds.Max.Z + 10.0);
    TestTrue(TEXT("Russian infantry have a rolled greatcoat"),
        BuildRegimentHistoricalDress(S::RussianLine).Count(F::Greatcoat) == 1);
    TestTrue(TEXT("Highlanders have an authored kilt"),
        BuildRegimentHistoricalDress(S::Highlander).Count(F::Kilt) >= 3);
    const auto HighlandProxy = BuildRegimentHistoricalProxy(S::Highlander);
    TestTrue(TEXT("The distant Highlander keeps the kilt and stands at ground level"),
        HighlandProxy.Count(F::Kilt) >= 3 && FMath::Abs(HighlandProxy.Bounds.Min.Z) < 2.0);
    const auto NumidianProxy = BuildRegimentHistoricalProxy(S::Numidian);
    TestTrue(TEXT("One horse and one rider share the distant Numidian mesh"),
        NumidianProxy.Count(F::Horse) == 1 && NumidianProxy.Bounds.Max.Z > 200.0);
    battle::UnitSpec LegacyRoman;
    TestTrue(TEXT("A legacy Roman infantry unit receives a legionary figure"),
        RegimentHistoricalStyle(S::None, TEXT("rome"), LegacyRoman, TEXT("ancient")) == S::Legionary);
    LegacyRoman.archer = true;
    LegacyRoman.ranged_weapon = W::Sling;
    TestTrue(TEXT("An actual slinger within a Roman regiment carries the loose-order figure"),
        RegimentHistoricalStyle(S::Legionary, TEXT("rome"), LegacyRoman, TEXT("ancient")) == S::LooseSling);
    TestTrue(TEXT("The stored French Young Guard profile resolves to the Young Guard figure"),
        RegimentHistoricalProfile(TEXT("young_guard")) == S::YoungGuard);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentCavalrySilhouetteTest, "RegimentBattle.Presentation.CavalrySilhouettes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentCavalrySilhouetteTest::RunTest(const FString&)
{
    using S = ERegimentHistoricalFigure;
    using F = ERegimentAncientFeature;
    using M = ERegimentAncientMaterial;
    using C = battle::CavalryClass;
    battle::UnitSpec Rider;
    Rider.troop = battle::UnitTroop::Mounted;
    const auto StyleFor = [&](const TCHAR* Faction, const TCHAR* Era, C Class)
    {
        Rider.cavalry = Class;
        return RegimentHistoricalStyle(S::None, Faction, Rider, Era);
    };
    const auto Livonian = StyleFor(TEXT("livonian_order"), TEXT("high_medieval"), C::Heavy);
    const auto LivonianLight = StyleFor(TEXT("livonian_order"), TEXT("high_medieval"), C::Light);
    const auto Cuirassier = StyleFor(TEXT("france"), TEXT("napoleonic"), C::Heavy);
    const auto Hussar = StyleFor(TEXT("france"), TEXT("napoleonic"), C::Light);
    const auto Austrian = StyleFor(TEXT("austria"), TEXT("renaissance"), C::Heavy);
    const auto AustrianLight = StyleFor(TEXT("austria"), TEXT("renaissance"), C::Light);
    const auto Saxon = StyleFor(TEXT("saxony"), TEXT("great_northern_war"), C::Heavy);
    const auto SaxonDragoon = StyleFor(TEXT("saxony"), TEXT("great_northern_war"), C::Light);
    const auto Cossack = StyleFor(TEXT("russia"), TEXT("napoleonic"), C::Light);
    const auto Numidian = StyleFor(TEXT("carthage"), TEXT("ancient"), C::Light);
    TestTrue(TEXT("Livonian heavy cavalry has its own silhouette"), Livonian != LivonianLight);
    TestTrue(TEXT("Don Cossacks are visually distinct from hussars"), Cossack != Hussar);
    TestEqual(TEXT("Light Austrian horse retains its early modern outfit"), AustrianLight, S::EarlyModernHorse);
    TestEqual(TEXT("Saxon dragoons retain their Great Northern War outfit"), SaxonDragoon, S::NorthernHorse);
    TestEqual(TEXT("Great Northern War Don Cossacks also use the light Cossack silhouette"),
        StyleFor(TEXT("russia"), TEXT("great_northern_war"), C::Light), Cossack);
    TestEqual(TEXT("Renaissance Cossack cavalry uses the light Cossack silhouette"),
        StyleFor(TEXT("cossacks"), TEXT("renaissance"), C::Light), Cossack);
    TestEqual(TEXT("Carthaginian light riders are Numidians"), Numidian, S::Numidian);

    const auto BodySize = [&](S Style)
    {
        const auto Proxy = BuildRegimentHistoricalProxy(Style);
        const auto* Body = Proxy.Parts.FindByPredicate([](const auto& Piece) { return Piece.Feature == F::Horse; });
        return Body ? Body->Transform.GetScale3D() : FVector::ZeroVector;
    };
    const FVector LivonianBody = BodySize(Livonian);
    const FVector CuirassierBody = BodySize(Cuirassier);
    const FVector CossackBody = BodySize(Cossack);
    const FVector NumidianBody = BodySize(Numidian);
    TestTrue(TEXT("Livonian horse has a substantially broader barrel than the light Cossack horse"),
        LivonianBody.Y >= CossackBody.Y + 12.0);
    TestTrue(TEXT("Cuirassier horse is longer and taller at the barrel than a hussar horse"),
        CuirassierBody.X >= BodySize(Hussar).X + 10.0 && CuirassierBody.Z > BodySize(Hussar).Z);
    TestTrue(TEXT("Austrian and Saxon cuirassiers also ride larger horses than their light counterparts"),
        BodySize(Austrian).X >= BodySize(AustrianLight).X + 10.0 &&
        BodySize(Saxon).X >= BodySize(SaxonDragoon).X + 10.0);
    TestTrue(TEXT("Numidian horse is narrower and shallower than a standard ancient horse"),
        NumidianBody.Y + 7.0 <= BodySize(S::RomanCavalry).Y && NumidianBody.Z + 6.0 <= BodySize(S::RomanCavalry).Z);
    TestTrue(TEXT("The Cossack horse keeps a lower barrel than the cuirassier horse"),
        CossackBody.Z + 8.0 <= CuirassierBody.Z);
    TestTrue(TEXT("The rider is higher over the larger horse and lower over the smallest one"),
        RegimentHistoricalSeatOffset(Livonian) > RegimentHistoricalSeatOffset(Cuirassier) &&
        RegimentHistoricalSeatOffset(Cuirassier) > RegimentHistoricalSeatOffset(Cossack) &&
        RegimentHistoricalSeatOffset(Cossack) > RegimentHistoricalSeatOffset(Numidian));

    for (const auto Style : {Livonian, Cuirassier, Austrian, Saxon, Cossack, Numidian})
    {
        const auto Mount = BuildRegimentHistoricalMount(Style);
        const auto Proxy = BuildRegimentHistoricalProxy(Style);
        TestTrue(TEXT("The close mount has one horse and stands at ground level"),
            Mount.Count(F::Horse) == 1 && FMath::Abs(Mount.Bounds.Min.Z) < 5.0);
        bool bAllPartsMatch = Proxy.Count(F::Horse) == 1;
        for (const auto& Part : Mount.Parts)
            bAllPartsMatch &= Proxy.Parts.ContainsByPredicate([&Part](const auto& ProxyPart)
            {
                return Part.Primitive == ProxyPart.Primitive && Part.Material == ProxyPart.Material &&
                    Part.Feature == ProxyPart.Feature && Part.Transform.Equals(ProxyPart.Transform);
            });
        TestTrue(TEXT("The distant silhouette preserves the close horse and its tack"), bAllPartsMatch);
    }

    const auto LivonianTack = BuildRegimentHistoricalTack(Livonian);
    TestTrue(TEXT("Livonian barding extends in front of the horse's shoulders"),
        LivonianTack.Bounds.Max.X > 65.0 && LivonianTack.Parts.ContainsByPredicate([](const auto& Part)
        {
            return Part.Material == M::Iron && Part.Transform.GetScale3D().Z >= 34.0;
        }));
    TestEqual(TEXT("The Numidian horse displays only a neck strap"), BuildRegimentHistoricalTack(Numidian).Parts.Num(), 1);
    const auto NapoleonicCrestHeight = BuildRegimentHistoricalDress(Cuirassier).Bounds.Max.Z;
    for (const auto Style : {Cuirassier, Austrian, Saxon})
    {
        const auto Dress = BuildRegimentHistoricalDress(Style);
        TestTrue(TEXT("A rounded iron breastplate projects from the cuirassier torso"),
            Dress.Parts.ContainsByPredicate([](const auto& Part)
            {
                return Part.Primitive == ERegimentAncientPrimitive::Ellipsoid && Part.Material == M::Iron &&
                    Part.Transform.GetLocation().X >= 15.0 &&
                    FMath::IsNearlyEqual(Part.Transform.GetLocation().Z, 110.0, 8.0) &&
                    Part.Transform.GetScale3D().Y >= 45.0;
            }));
        if (Style != Cuirassier)
            TestTrue(TEXT("Earlier headgear has a lower silhouette than the Napoleonic helmet crest"),
                Dress.Bounds.Max.Z + 5.0 < NapoleonicCrestHeight);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentFigureRefreshTest, "RegimentBattle.Presentation.FigureRefresh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentFigureRefreshTest::RunTest(const FString&)
{
    using S = ERegimentHistoricalFigure;
    battle::UnitSpec Soldier;
    for (const auto* Faction : {TEXT("egypt"), TEXT("porus"), TEXT("carthage"), TEXT("gauls"),
            TEXT("scythians"), TEXT("parthians"), TEXT("sarmatians"), TEXT("diadochi")})
    {
        const auto Style = RegimentHistoricalStyle(S::None, Faction, Soldier, TEXT("ancient"));
        TestTrue(FString::Printf(TEXT("%s infantry keep a dressed silhouette in both LODs"), Faction),
            Style != S::None && BuildRegimentHistoricalDress(Style).Bounds.IsValid &&
            BuildRegimentHistoricalProxy(Style).Bounds.IsValid);
    }
    TestTrue(TEXT("The length of an auxiliary spear differs from a hoplite spear"),
        FMath::Abs(RegimentHistoricalSpearLength(S::Auxiliary) - RegimentHistoricalSpearLength(S::Hoplite)) >= 25.0);

    TMap<S, bool> CheckedFigures;
    using T = battle::UnitTroop;
    using R = ERegimentEraRole;
    const auto CheckOutfit = [&](const FRegimentEraDefinition& Era, const FRegimentEraFaction& Faction,
        R Role, const FRegimentEraTroop& Troop)
    {
        Soldier.troop = Role == R::Mounted ? T::Mounted : Role == R::Spearmen ? T::Spearman : T::Infantry;
        Soldier.archer = Role == R::Archers;
        Soldier.ranged_weapon = Troop.RangedWeapon;
        Soldier.cavalry = Troop.Cavalry;
        const auto Style = RegimentHistoricalStyle(S::None, Faction.Id, Soldier, Era.Id);
        if (!TestTrue(FString::Printf(TEXT("%s/%s has a historical figure"), *Era.Id, *Troop.Name), Style != S::None))
            return;
        if (!CheckedFigures.Contains(Style))
        {
            const auto Near = BuildRegimentHistoricalDress(Style);
            const auto Far = BuildRegimentHistoricalProxy(Style);
            TestTrue(TEXT("Each selected outfit has bounded geometry and a simplified proxy"),
                Near.Bounds.IsValid && Far.Bounds.IsValid && Far.bSimplified && Near.Parts.Num() > 3);
            CheckedFigures.Add(Style, Far.Count(ERegimentAncientFeature::Horse) == 1);
        }
        TestEqual(TEXT("Exactly one horse belongs to each mounted outfit"), CheckedFigures.FindChecked(Style),
            Role == R::Mounted);
    };
    for (const auto& Era : RegimentEraCatalog::All())
    {
        if (Era.Id == TEXT("sengoku") || Era.Id == TEXT("fantasy"))
            continue;
        for (const auto& Faction : Era.Factions)
        {
            for (const auto Role : {R::Swords, R::Spearmen, R::Archers, R::Mounted})
                CheckOutfit(Era, Faction, Role, Faction.Troops[int32(Role)]);
            for (const auto& Variant : Faction.MountVariants)
                if (Variant)
                    CheckOutfit(Era, Faction, R::Mounted, *Variant);
        }
    }

    Soldier.troop = T::Mounted;
    Soldier.cavalry = battle::CavalryClass::Heavy;
    TestEqual(TEXT("Polish heavy cavalry wear cuirassier equipment"),
        RegimentHistoricalStyle(S::None, TEXT("poland"), Soldier, TEXT("napoleonic")), S::NapoleonicCuirassier);
    Soldier.cavalry = battle::CavalryClass::Light;
    TestEqual(TEXT("Polish light cavalry keep the lancer outfit"),
        RegimentHistoricalStyle(S::None, TEXT("poland"), Soldier, TEXT("napoleonic")), S::NapoleonicLancer);

    using H = ERegimentAncientShield;
    TestTrue(TEXT("Anglo-Saxon and Rus infantry keep their configured shield shapes"),
        RegimentHistoricalShield(S::WesternMail, ERegimentEraShield::Round) == H::RoundMedieval &&
        RegimentHistoricalShield(S::EasternMail, ERegimentEraShield::Kite) == H::Kite);
    TSet<H> GreekShields;
    TSet<double> SpearLengths;
    const double Dory = RegimentHistoricalSpearLength(S::Hoplite);
    const double Sarissa = RegimentHistoricalSpearLength(S::Sarissa);
    for (uint64 Id = 1; Id <= 128; ++Id)
    {
        const double Length = RegimentAncientSpearLength(Dory, Id);
        SpearLengths.Add(Length);
        TestTrue(TEXT("All shaft variations stay within their troop silhouette"),
            Length >= Dory * .92 - .01 && Length <= Dory * 1.08 + .01 &&
            RegimentAncientSpearLength(Sarissa, Id) >= Sarissa * .96 - .01);
        TestEqual(TEXT("The same soldier keeps the same spear"), RegimentAncientSpearLength(Dory, Id), Length);
        GreekShields.Add(RegimentPaintedShieldStyle(H::Aspis, TEXT("greece"), Id));
        const auto Spartan = RegimentPaintedShieldStyle(H::Aspis, TEXT("sparta"), Id);
        TestTrue(TEXT("Spartan shields mostly share a lambda and keep plain backup faces"),
            Spartan == H::Aspis || Spartan == H::AspisLambda);
        TestEqual(TEXT("Other shield shapes keep their original identity"),
            RegimentPaintedShieldStyle(H::Scutum, TEXT("rome"), Id), H::Scutum);
    }
    TestEqual(TEXT("Four aspis paint patterns are available"), GreekShields.Num(), 4);
    TestEqual(TEXT("Five stable shaft lengths are available"), SpearLengths.Num(), 5);
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    int32 HorseSlot = INDEX_NONE;
    int32 CoatOfArmsSlot = INDEX_NONE;
    const auto* Cavalry = MakeRegimentAncientMesh(*GetTransientPackage(),
        BuildRegimentHistoricalProxy(S::NapoleonicCuirassier), Material, CoatOfArmsSlot, &HorseSlot);
    TestTrue(TEXT("The distant horse coat can be tinted independently of heraldry"),
        Cavalry && Cavalry->GetStaticMaterials().IsValidIndex(HorseSlot) && HorseSlot != CoatOfArmsSlot);
    for (const auto Style : GreekShields)
    {
        const auto Shield = BuildRegimentAncientShield(Style);
        TestTrue(TEXT("Each pattern keeps a round base and the same equipment size"),
            FMath::IsNearlyEqual(Shield.Bounds.GetSize().Y, Shield.Bounds.GetSize().Z) &&
            Shield.Bounds.GetSize().Y >= 65.0);
        int32 PaintSlot = INDEX_NONE;
        const auto* Mesh = MakeRegimentAncientMesh(*GetTransientPackage(), Shield, Material, PaintSlot);
        TestTrue(TEXT("Each painted shield bakes with a faction color slot"),
            Mesh && Mesh->GetStaticMaterials().IsValidIndex(PaintSlot));
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentSengokuFigureTest, "RegimentBattle.Presentation.SengokuFigures",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentSengokuFigureTest::RunTest(const FString&)
{
    using S = ERegimentHistoricalFigure;
    using F = ERegimentAncientFeature;
    using W = battle::RangedWeapon;
    using T = battle::UnitTroop;
    battle::UnitSpec Figure;
    TestEqual(TEXT("Swordsmen wear samurai armour"), RegimentSengokuStyle(Figure), S::SengokuSamurai);
    Figure.troop = T::Spearman;
    TestEqual(TEXT("Spearmen wear ashigaru equipment"), RegimentSengokuStyle(Figure), S::SengokuAshigaru);
    Figure.troop = T::Infantry;
    Figure.archer = true;
    Figure.ranged_weapon = W::Bow;
    TestEqual(TEXT("Archers use the yumi outfit"), RegimentSengokuStyle(Figure), S::SengokuArcher);
    Figure.ranged_weapon = W::Gunpowder;
    TestEqual(TEXT("Gunners use the arquebusier outfit"), RegimentSengokuStyle(Figure), S::SengokuArquebusier);
    Figure.archer = false;
    Figure.troop = T::Mounted;
    TestEqual(TEXT("Cavalry keep a separate mounted figure"), RegimentSengokuStyle(Figure), S::SengokuCavalry);

    for (const auto Style : {S::SengokuSamurai, S::SengokuAshigaru, S::SengokuArcher,
            S::SengokuArquebusier, S::SengokuCavalry})
    {
        const auto Near = BuildRegimentHistoricalDress(Style);
        const auto Far = BuildRegimentHistoricalProxy(Style);
        const bool bArmored = Style == S::SengokuSamurai || Style == S::SengokuCavalry;
        const auto Hat = bArmored ? F::Kabuto : F::Jingasa;
        TestTrue(TEXT("The period headgear remains visible in both meshes"), Near.Count(Hat) == 1 && Far.Count(Hat) == 1);
        TestTrue(TEXT("Both meshes preserve faction material"),
            Near.Parts.ContainsByPredicate([](const auto& Part) { return Part.Material == ERegimentAncientMaterial::Paint; }) &&
            Far.Parts.ContainsByPredicate([](const auto& Part) { return Part.Material == ERegimentAncientMaterial::Paint; }));
        TestEqual(TEXT("The large shoulder guards identify armour"), Near.Count(F::Sode), bArmored ? 2 : 0);
        TestTrue(TEXT("The proxy is simplified"), Far.bSimplified);
        TestEqual(TEXT("Only the cavalry proxy contains a horse"), Far.Count(F::Horse), Style == S::SengokuCavalry ? 1 : 0);
    }
    const auto Yumi = BuildRegimentSengokuWeapon(S::SengokuArcher);
    TestTrue(TEXT("The yumi has an extended upper limb above the grip"),
        Yumi.Bounds.IsValid && Yumi.Bounds.GetSize().Z >= 150.0 && Yumi.Bounds.Max.Z > -Yumi.Bounds.Min.Z * 1.6);
    TestTrue(TEXT("The arquebus has an identifiable matchlock and long horizontal silhouette"),
        BuildRegimentSengokuWeapon(S::SengokuArquebusier).Count(F::Matchlock) == 1 &&
        BuildRegimentSengokuWeapon(S::SengokuArquebusier).Bounds.GetSize().X > 100.0);
    TestTrue(TEXT("Mounted tack and a cloth field sign have individual shapes"),
        BuildRegimentHistoricalTack(S::SengokuCavalry).Parts.Num() > 3 &&
        BuildRegimentSengokuBanner(0).Count(F::Sashimono) == 1);
    return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentSengokuBannerTest, "RegimentBattle.Presentation.SengokuBannerSilhouettes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentSengokuBannerTest::RunTest(const FString&)
{
    using F = ERegimentAncientFeature;
    TSet<double> Heights, Widths, PoleTops;
    TSet<int32> Variants;
    for (uint64 Id = 6; Id <= 240; Id += 6)
    {
        const int32 Variant = RegimentSengokuBannerVariant(Id);
        TestEqual(TEXT("A wearer keeps the same flag across frames and LODs"), RegimentSengokuBannerVariant(Id), Variant);
        if (!TestTrue(TEXT("A wearer maps to an available cached banner"), Variant >= 0 && Variant < RegimentSengokuBannerVariantCount))
            continue;
        Variants.Add(Variant);
    }
    TestEqual(TEXT("Wearers in one formation get all banner shapes"), Variants.Num(), RegimentSengokuBannerVariantCount);
    for (const int32 Variant : Variants)
    {
        const auto Banner = BuildRegimentSengokuBanner(Variant);
        const auto* Cloth = Banner.Parts.FindByPredicate([](const auto& Part) { return Part.Feature == F::Sashimono; });
        if (!TestNotNull(TEXT("The back banner has an identifiable cloth panel"), Cloth))
            continue;
        const FVector Size = Cloth->Transform.GetScale3D();
        TestTrue(TEXT("The clan color remains on a narrow, readable cloth panel"),
            Cloth->Material == ERegimentAncientMaterial::Paint &&
            Size.Y >= 25.0 && Size.Y <= 40.0 && Size.Z >= 50.0 && Size.Z <= 80.0);
        TestTrue(TEXT("The L-shaped staff meets the cuirass at the same mounting point"),
            Banner.Parts.ContainsByPredicate([](const auto& Part)
            {
                return Part.Material == ERegimentAncientMaterial::Wood &&
                    Part.Transform.InverseTransformPosition(FVector(-17, 10, 94)).GetAbsMax() <= .5;
            }));
        Heights.Add(Size.Z);
        Widths.Add(Size.Y);
        PoleTops.Add(Banner.Bounds.Max.Z);
    }
    TestTrue(TEXT("Sashimono vary in cloth width, cloth height and pole height"),
        Heights.Num() >= 3 && Widths.Num() >= 3 && PoleTops.Num() >= 3);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentSengokuSaddleTest, "RegimentBattle.Presentation.SengokuSaddle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentSengokuSaddleTest::RunTest(const FString&)
{
    using S = ERegimentHistoricalFigure;
    using F = ERegimentAncientFeature;
    const auto Near = BuildRegimentHistoricalMount(S::SengokuCavalry);
    const auto Far = BuildRegimentHistoricalProxy(S::SengokuCavalry);
    TestEqual(TEXT("Mounted samurai use a Japanese mount with both rendering paths"), Near.Count(F::Horse), 1);
    const auto IsHorse = [](const auto& Part) { return Part.Feature == F::Horse; };
    const auto* NearHorse = Near.Parts.FindByPredicate(IsHorse);
    const auto* FarHorse = Far.Parts.FindByPredicate(IsHorse);
    if (!TestNotNull(TEXT("The near mounted samurai has a horse barrel"), NearHorse) ||
        !TestNotNull(TEXT("The far mounted samurai has a horse barrel"), FarHorse))
        return false;
    TestTrue(TEXT("Both rendering paths use the same horse silhouette"), NearHorse->Transform.Equals(FarHorse->Transform));
    const double Back = FarHorse->Transform.GetLocation().Z + FarHorse->Transform.GetScale3D().Z * .5;
    TestTrue(TEXT("The seated proxy pelvis clears the back of the horse"),
        RegimentHistoricalSeatOffset(S::SengokuCavalry) + 65.0 > Back);
    const auto IsSeat = [](const auto& Part)
    {
        return Part.Material == ERegimentAncientMaterial::Leather &&
            Part.Transform.InverseTransformPosition(FVector(0, 0, RegimentSengokuSaddleHeight - 1)).GetAbsMax() <= .5;
    };
    TestTrue(TEXT("The saddle has a surface beneath the rider in both meshes"),
        Near.Parts.ContainsByPredicate(IsSeat) && Far.Parts.ContainsByPredicate(IsSeat));
    return !HasAnyErrors();
}

#endif
