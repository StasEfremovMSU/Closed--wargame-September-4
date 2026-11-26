#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "RegimentGrassPresentation.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "StaticMeshResources.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentBattlefieldGrassTest, "RegimentBattle.Presentation.BattlefieldGrass",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentBattlefieldGrassTest::RunTest(const FString&)
{
    using T = battle::MarchTerrain;
    using F = battle::TerrainFeature;
    const battle::MarchField SmallField{120, 76};
    const battle::Obstacle Areas[] = {
        {-60.f, -3.f, 60.f, 3.f, false, F::Road},
        {-4.f, -38.f, 4.f, 38.f, true, F::River},
        {-42.f, 12.f, -11.f, 32.f, false, F::PloughedField},
        {20.f, -32.f, 49.f, -14.f, false, F::Marsh}
    };
    const auto Hill = battle::MarchHill(T::Hill, SmallField);
    const auto Grass = BuildRegimentGrassTransforms(T::Hill, SmallField, &Hill.value(), Areas);
    const auto Repeated = BuildRegimentGrassTransforms(T::Hill, SmallField, &Hill.value(), Areas);
    TestTrue(TEXT("The meadow receives ample grass"), Grass.Num() > 850);
    TestEqual(TEXT("Repeat generation yields the same count"), Repeated.Num(), Grass.Num());

    bool bRaised = false;
    for (int32 Index = 0; Index < Grass.Num(); ++Index)
    {
        const FVector Point = Grass[Index].GetLocation();
        if (!Repeated.IsValidIndex(Index) || !Grass[Index].Equals(Repeated[Index], 0.0))
            return TestTrue(TEXT("Grass placement is deterministic"), false);
        if (FMath::Abs(Point.X) >= 6000.0 || FMath::Abs(Point.Y) >= 3800.0)
            return TestTrue(TEXT("Every clump stays inside the field"), false);
        for (const auto& Area : Areas)
            if (Point.X > 100.0 * Area.min_x - 60.0 && Point.X < 100.0 * Area.max_x + 60.0 &&
                Point.Y > 100.0 * Area.min_y - 60.0 && Point.Y < 100.0 * Area.max_y + 60.0)
                return TestTrue(TEXT("Clumps leave clearance at roads, water, crops and marsh"), false);

        const double Surface = 100.0 * battle::HillHeight(*Hill, {float(Point.X / 100), float(Point.Y / 100), 0.f});
        if (Point.Z > Surface || Point.Z < Surface - 10.0)
            return TestTrue(TEXT("Roots are sunk into the hill rather than floating above it"), false);
        bRaised |= Point.Z > 100.0;
    }
    TestTrue(TEXT("Grass follows the rising terrain"), bRaised);

    const auto RiverAreas = battle::MarchTerrainAreas(T::River, SmallField);
    const auto River = BuildRegimentGrassTransforms(T::River, SmallField, nullptr, RiverAreas);
    TestTrue(TEXT("Grass grows on the river meadows"), !River.IsEmpty());
    for (const auto& Instance : River)
        if (FMath::Abs(Instance.GetLocation().X) < 100.0 * battle::MarchRiverHalfWidth + 60.0)
            return TestTrue(TEXT("The entire river channel and its crossings are clear"), false);

    for (const auto Winter : {T::Peipus, T::Narva})
        TestTrue(TEXT("Snow and ice have no green grass"),
            BuildRegimentGrassTransforms(Winter, {600, 400}, nullptr, {}).IsEmpty());
    const auto Large = BuildRegimentGrassTransforms(T::Waterloo, {600, 400}, nullptr, {});
    TestTrue(TEXT("A large battlefield stays under the 12,000-clump budget"),
        Large.Num() > 9000 && Large.Num() <= 12000);
    TestTrue(TEXT("Grass is culled outside the near camera view"), RegimentGrassCullDistanceCm <= 5200);

    auto* Material = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (!TestNotNull(TEXT("The bundled grass material is available"), Material))
        return false;
    UStaticMesh* Mesh = MakeRegimentGrassMesh(*GetTransientPackage(), *Material);
    if (!TestNotNull(TEXT("The single opaque tuft mesh builds"), Mesh))
        return false;
    const auto& Lods = Mesh->GetRenderData()->LODResources;
    return TestTrue(TEXT("Each clump uses one section and at most 16 triangles"),
        Lods.Num() == 1 && Lods[0].Sections.Num() == 1 &&
        Lods[0].GetNumTriangles() > 0 && Lods[0].GetNumTriangles() <= 16);
}
#endif
