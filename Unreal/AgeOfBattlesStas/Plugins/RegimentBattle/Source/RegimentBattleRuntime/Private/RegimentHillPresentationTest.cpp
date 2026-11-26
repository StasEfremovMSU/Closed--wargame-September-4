#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "RegimentHillPresentation.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Scenario.h"
THIRD_PARTY_INCLUDES_END

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentHillPresentationTest, "RegimentBattle.Presentation.HillSurface",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentHillPresentationTest::RunTest(const FString& Parameters)
{
    const battle::TerrainHill Hill;
    const auto Mesh = BuildRegimentHillMesh(Hill, 8400.0, 5300.0);
    TestTrue(TEXT("The hill and plain form one tessellated surface"), Mesh.TriangleCount() > 1000);
    const auto Bounds = Mesh.GetBounds();
    TestTrue(TEXT("Board edges are flat at the assigned width and depth"),
        FMath::IsNearlyEqual(Bounds.Max.X, 8400.0) && FMath::IsNearlyEqual(Bounds.Max.Y, 5300.0) &&
        FMath::IsNearlyZero(Bounds.Min.Z));
    TestTrue(TEXT("The summit and slope triangles face above the field"), Mesh.GetTriNormal(0).Z > 0.0);

    TSet<int32> SurfaceVertices;
    const auto* Materials = Mesh.Attributes()->GetMaterialID();
    for (const int32 TriangleId : Mesh.TriangleIndicesItr())
    {
        if (Materials->GetValue(TriangleId) != 0)
            continue;
        const auto Triangle = Mesh.GetTriangle(TriangleId);
        SurfaceVertices.Add(Triangle.A);
        SurfaceVertices.Add(Triangle.B);
        SurfaceVertices.Add(Triangle.C);
    }
    double SummitZ = 0.0;
    for (const int32 VertexId : SurfaceVertices)
    {
        const FVector3d Vertex = Mesh.GetVertex(VertexId);
        SummitZ = FMath::Max(SummitZ, Vertex.Z);
        const battle::Vec3 CorePosition{float(Vertex.X / 100), float(Vertex.Y / 100), 0};
        const FVector Foot = RegimentTerrainLocation(CorePosition, FVector(0, 0, 35), &Hill);
        if (!FMath::IsNearlyEqual(Foot.Z - 35.0, Vertex.Z, 0.02))
        {
            AddError(FString::Printf(TEXT("Vertex %d diverges from the grounded unit transform"), VertexId));
            break;
        }
    }
    TestEqual(TEXT("The mesh summit height matches BattleCore"), SummitZ, double(Hill.height_m) * 100.0);
    TestEqual(TEXT("A flat board retains the regular transform"),
        RegimentTerrainLocation({1, 2, 0}, FVector(4, 5, 6), nullptr), FVector(104, 205, 6));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentRedoubtSurfaceTest, "RegimentBattle.Presentation.RedoubtSurface",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentRedoubtSurfaceTest::RunTest(const FString&)
{
    using T = battle::TerrainFeature;
    const battle::Obstacle Areas[] = {
        {-10.f, -6.f, -4.f, 6.f, false, T::RedoubtRamp},
        {-4.f, -6.f, 8.f, -4.f, true, T::RedoubtBank},
        {-4.f, 4.f, 8.f, 6.f, true, T::RedoubtBank},
        {-4.f, -4.f, 8.f, 4.f, false, T::RedoubtInterior}
    };
    const battle::TerrainHill Hill;
    const auto Mesh = BuildRegimentHillMesh(Hill, 8400.0, 5300.0, Areas);
    bool bDitch = false, bRamp = false, bBank = false, bRearChecked = false;
    for (const int32 Id : Mesh.VertexIndicesItr())
    {
        const FVector P = Mesh.GetVertex(Id);
        const FVector Foot = RegimentTerrainLocation({float(P.X / 100), float(P.Y / 100), 0}, FVector::ZeroVector, &Hill);
        const double Relief = P.Z - Foot.Z;
        if (P.X < -400.0 && FMath::Abs(P.Y) < 400.0)
        {
            bDitch |= Relief < -20.0;
            bRamp |= Relief > 25.0;
        }
        if (P.X > -400.0 && P.X < 800.0 && FMath::Abs(P.Y) > 400.0 && FMath::Abs(P.Y) < 600.0)
            bBank |= Relief > 150.0;
        if (P.X >= 800.0 && P.X < 1000.0 && FMath::Abs(P.Y) < 400.0)
        {
            bRearChecked = true;
            if (FMath::Abs(Relief) >= .1)
                return TestTrue(TEXT("Rear opening stays flush to the gameplay surface"), false);
        }
    }
    TestTrue(TEXT("The mesh includes the rear opening"), bRearChecked);
    TestTrue(TEXT("The front ditch is cut into the ground"), bDitch);
    TestTrue(TEXT("The entry rises on the inner side of the ditch"), bRamp);
    TestTrue(TEXT("The side embankments reach the tops of their parapets"), bBank);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentRoadSurfaceTest, "RegimentBattle.Presentation.RoadSurface",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentRoadSurfaceTest::RunTest(const FString&)
{
    const battle::Obstacle Area{-110.f, -2.1f, 110.f, 2.1f, false, battle::TerrainFeature::Road};
    const auto Mesh = BuildRegimentRoadMesh(nullptr, 11000.0, 7000.0, {&Area, 1});
    if (!TestTrue(TEXT("The road is one bounded subdivided mesh"), Mesh.TriangleCount() > 100 && Mesh.TriangleCount() < 3000))
        return false;
    const auto Bounds = Mesh.GetBounds();
    TestTrue(TEXT("Irregular shoulders fit inside the field and the road corridor"),
        Bounds.Min.X >= -11000.0 && Bounds.Max.X <= 11000.0 && Bounds.Min.Y >= -211.0 && Bounds.Max.Y <= 211.0);
    int32 TrackSides[2] = {};
    const auto* Materials = Mesh.Attributes()->GetMaterialID();
    for (const int32 Id : Mesh.TriangleIndicesItr())
        if (Materials->GetValue(Id) == 1)
        {
            const auto Triangle = Mesh.GetTriangle(Id);
            const double Across = Mesh.GetVertex(Triangle.A).Y + Mesh.GetVertex(Triangle.B).Y + Mesh.GetVertex(Triangle.C).Y;
            ++TrackSides[Across > 0.0 ? 1 : 0];
        }
    TestTrue(TEXT("Both parallel wheel tracks share the rut material and span the road"),
        TrackSides[0] > 70 && TrackSides[1] == TrackSides[0]);
    return true;
}
#endif
