#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "RegimentWallPresentation.h"

THIRD_PARTY_INCLUDES_START
#include "battle/MarchMission.h"
THIRD_PARTY_INCLUDES_END

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentWallPresentationTest, "RegimentBattle.Presentation.FieldWalls",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentWallPresentationTest::RunTest(const FString& Parameters)
{
    const auto Obstacles = battle::MarchObstacles(battle::MarchTerrain::StoneWalls);
    const auto Mesh = BuildRegimentWallMesh(Obstacles);
    TestTrue(TEXT("The mesh contains faceted masonry"), Mesh.TriangleCount() > 200);
    TSet<int32> Materials;
    const auto* MaterialIDs = Mesh.Attributes()->GetMaterialID();
    bool bHasTopFace = false;
    for (const int32 Id : Mesh.TriangleIndicesItr())
    {
        Materials.Add(MaterialIDs->GetValue(Id));
        bHasTopFace |= MaterialIDs->GetValue(Id) == int32(ERegimentWallMaterial::Coping) &&
            Mesh.GetTriNormal(Id).Z > .9;
    }
    TestEqual(TEXT("Mortar, ashlar, light stones and coping appear"), Materials.Num(), 4);
    TestTrue(TEXT("Coping is visible from overhead"), bHasTopFace);

    for (const int32 Id : Mesh.VertexIndicesItr())
    {
        const FVector3d Point = Mesh.GetVertex(Id) / 100.0;
        if (std::ranges::none_of(Obstacles, [&](const battle::Obstacle& Box)
            {
                return Box.feature == battle::TerrainFeature::StoneWall &&
                    Point.X >= Box.min_x && Point.X <= Box.max_x && Point.Y >= Box.min_y && Point.Y <= Box.max_y;
            }))
        {
            AddError(FString::Printf(TEXT("A wall vertex extends into a navigable area at %s"), *Point.ToString()));
            break;
        }
    }
    return true;
}
#endif
