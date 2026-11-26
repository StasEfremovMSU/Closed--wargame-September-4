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
        RegimentHistoricalStyle(S::None, TEXT("rome"), LegacyRoman, true, false) == S::Legionary);
    LegacyRoman.archer = true;
    LegacyRoman.ranged_weapon = W::Sling;
    TestTrue(TEXT("An actual slinger within a Roman regiment carries the loose-order figure"),
        RegimentHistoricalStyle(S::Legionary, TEXT("rome"), LegacyRoman, true, false) == S::LooseSling);
    TestTrue(TEXT("The stored French Young Guard profile resolves to the Young Guard figure"),
        RegimentHistoricalProfile(TEXT("young_guard")) == S::YoungGuard);
    return !HasAnyErrors();
}
#endif
