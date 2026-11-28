#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentAncientFigures.h"
#include "RegimentBattlefieldEffects.h"
#include "RegimentFieldGunFigure.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "StaticMeshResources.h"

THIRD_PARTY_INCLUDES_START
#include "battle/MarchMission.h"
#include "battle/World.h"
THIRD_PARTY_INCLUDES_END

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentFieldGunFigureTest,
    "RegimentBattle.Presentation.FieldGunFigure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentFieldGunFigureTest::RunTest(const FString&)
{
    using F = ERegimentAncientFeature;
    using P = ERegimentAncientPrimitive;
    const auto Cannon = BuildRegimentAncientPlatform(battle::BattlePlatform::FieldGun);
    TestEqual(TEXT("Two open wheels frame the carriage"), Cannon.Count(F::WheelRim), 2);
    TestEqual(TEXT("Spokes span both complete wheels"), Cannon.Count(F::WheelSpoke), 24);
    TestEqual(TEXT("Gunner and loader are modeled into the batch"), Cannon.Count(F::EngineCrew), 2);
    TestTrue(TEXT("The carriage and crew fit their 3.8 m slot spacing"),
        Cannon.Bounds.GetSize().X < 380.0 && Cannon.Bounds.GetSize().Y < 380.0);
    const auto* Lip = Cannon.Parts.FindByPredicate([](const auto& Part) { return Part.Feature == F::MuzzleLip; });
    if (!TestNotNull(TEXT("The barrel has a leading muzzle ring"), Lip))
        return false;
    TestTrue(TEXT("The muzzle has a recessed opening"), Lip->Primitive == P::Ring);
    TestTrue(TEXT("The model and firing effect share the same muzzle endpoint"),
        Lip->Transform.TransformPosition(FVector(0, 0, .5)).Equals(RegimentFieldGun::MuzzleOffset(), .01));

    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (!TestNotNull(TEXT("The cannon painting material is available"), Material))
        return false;
    int32 PaintSlot = INDEX_NONE;
    const auto* Mesh = MakeRegimentAncientMesh(*GetTransientPackage(), Cannon, Material, PaintSlot);
    if (!TestNotNull(TEXT("The cannon bakes into one instanced mesh"), Mesh))
        return false;
    if (!TestTrue(TEXT("Faction paint is retained and the mesh has an LOD"),
        Mesh->GetStaticMaterials().IsValidIndex(PaintSlot) && Mesh->GetRenderData() &&
        Mesh->GetRenderData()->LODResources.IsValidIndex(0)))
        return false;
    const auto& Sections = Mesh->GetRenderData()->LODResources[0].Sections;
    uint32 Triangles = 0;
    for (const auto& Section : Sections)
        Triangles += Section.NumTriangles;
    TestTrue(TEXT("Artillery stays in a compact mesh and material budget"),
        Triangles > 0 && Triangles <= 3500 && Sections.Num() <= 8);

    battle::World World;
    World.tick = 3;
    battle::UnitState Gun;
    Gun.id = {1};
    Gun.platform = battle::BattlePlatform::FieldGun;
    Gun.pose.yaw = battle::Pi / 2;
    World.units = {Gun};
    battle::BattleEvent Smoke;
    Smoke.kind = battle::EventKind::SmokeCreated;
    Smoke.source = Gun.id;
    World.events = {Smoke};
    FRegimentBattlefieldEffects Effects;
    TArray<FRegimentEffectSprite> Sprites;
    const FVector Root(120, -300, 18);
    const FRegimentEffectView View{Root + FVector(-600, 0, 120)};
    Effects.ObserveStep(World, Root, battle::MarchTerrain::OpenField, View.Location);
    Effects.Collect(World, Root, View, Sprites);
    if (!TestEqual(TEXT("A confirmed shot creates one cannon puff"), Sprites.Num(), 1))
        return false;
    TestTrue(TEXT("The cannon puff starts at the muzzle after yaw and translation"),
        Sprites[0].Center.Equals(RegimentFieldGun::MuzzleAt(Root, Gun.pose.yaw), .01));
    TestTrue(TEXT("The instanced recoil settles within half a second"),
        RegimentFieldGun::RecoilAt(0.0) > RegimentFieldGun::RecoilAt(3.0) &&
        RegimentFieldGun::RecoilAt(3.0) > 0.0 && RegimentFieldGun::RecoilAt(10.0) == 0.0);
    return !HasAnyErrors();
}
#endif
