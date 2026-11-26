#include "RegimentWallPresentation.h"

#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Math/RandomStream.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Scenario.h"
THIRD_PARTY_INCLUDES_END

namespace
{
using namespace UE::Geometry;

void AddStone(FDynamicMesh3& Mesh, const FVector3d& Minimum, const FVector3d& Maximum,
    ERegimentWallMaterial Material)
{
    const FVector3d Size = Maximum - Minimum;
    if (Size.GetMin() <= 0.0)
        return;

    // Inset the top and foot of each ashlar and clip its plan corners. Separate
    // facet normals keep the masonry readable from above and at ground level.
    const double Chamfer = FMath::Min(9.0, Size.GetMin() * .18);
    constexpr int32 Corners = 8;
    int32 Rings[4][Corners];
    for (int32 Ring = 0; Ring < 4; ++Ring)
    {
        const double Inset = Ring == 0 || Ring == 3 ? Chamfer * .6 : 0.0;
        const double ZLevels[] = {Minimum.Z, Minimum.Z + Chamfer, Maximum.Z - Chamfer, Maximum.Z};
        const double X0 = Minimum.X + Inset, X1 = Maximum.X - Inset;
        const double Y0 = Minimum.Y + Inset, Y1 = Maximum.Y - Inset;
        const FVector2d Outline[] = {
            {X0 + Chamfer, Y0}, {X1 - Chamfer, Y0}, {X1, Y0 + Chamfer}, {X1, Y1 - Chamfer},
            {X1 - Chamfer, Y1}, {X0 + Chamfer, Y1}, {X0, Y1 - Chamfer}, {X0, Y0 + Chamfer}
        };
        for (int32 Corner = 0; Corner < Corners; ++Corner)
            Rings[Ring][Corner] = Mesh.AppendVertex({Outline[Corner].X, Outline[Corner].Y, ZLevels[Ring]});
    }
    auto* Normals = Mesh.Attributes()->PrimaryNormals();
    auto* UVs = Mesh.Attributes()->PrimaryUV();
    const auto AddFace = [&](int32 A, int32 B, int32 C)
    {
        const int32 Index = Mesh.AppendTriangle(A, C, B);
        if (Index < 0)
            return;
        const FVector3d P = Mesh.GetVertex(A), Q = Mesh.GetVertex(B), R = Mesh.GetVertex(C);
        const FVector3f Normal(FVector3d::CrossProduct(Q - P, R - P).GetSafeNormal());
        Normals->SetTriangle(Index, FIndex3i(Normals->AppendElement(Normal), Normals->AppendElement(Normal),
            Normals->AppendElement(Normal)));
        UVs->SetTriangle(Index, FIndex3i(UVs->AppendElement({0.0f, 0.0f}), UVs->AppendElement({1.0f, 1.0f}),
            UVs->AppendElement({1.0f, 0.0f})));
        Mesh.Attributes()->GetMaterialID()->SetValue(Index, int32(Material));
    };
    for (int32 Ring = 0; Ring < 3; ++Ring)
        for (int32 Corner = 0; Corner < Corners; ++Corner)
        {
            const int32 Next = (Corner + 1) % Corners;
            AddFace(Rings[Ring][Corner], Rings[Ring][Next], Rings[Ring + 1][Next]);
            AddFace(Rings[Ring][Corner], Rings[Ring + 1][Next], Rings[Ring + 1][Corner]);
        }
    for (int32 Corner = 1; Corner + 1 < Corners; ++Corner)
    {
        AddFace(Rings[0][0], Rings[0][Corner + 1], Rings[0][Corner]);
        AddFace(Rings[3][0], Rings[3][Corner], Rings[3][Corner + 1]);
    }
}

void AddWall(FDynamicMesh3& Mesh, const battle::Obstacle& Wall)
{
    constexpr double Cm = 100.0;
    const double X0 = Cm * Wall.min_x, X1 = Cm * Wall.max_x;
    const double Y0 = Cm * Wall.min_y, Y1 = Cm * Wall.max_y;
    const bool AlongY = Y1 - Y0 >= X1 - X0;
    const double AlongStart = AlongY ? Y0 : X0, AlongEnd = AlongY ? Y1 : X1;
    const double FaceStart = AlongY ? X0 : Y0, FaceEnd = AlongY ? X1 : Y1;
    const auto Block = [&](double First, double Last, double Bottom, double Top, double Inset,
                       ERegimentWallMaterial Material)
    {
        const double Low = FaceStart + Inset, High = FaceEnd - Inset;
        AddStone(Mesh, AlongY ? FVector3d(Low, First, Bottom) : FVector3d(First, Low, Bottom),
            AlongY ? FVector3d(High, Last, Top) : FVector3d(Last, High, Top), Material);
    };

    // Original field-wall modules: plinth, interleaved dressed stone, and individually
    // tooled coping blocks. Every module uses the simulation envelope, including clipped ends.
    const int32 Panels = FMath::Max(1, FMath::CeilToInt((AlongEnd - AlongStart) / 480.0));
    for (int32 Panel = 0; Panel < Panels; ++Panel)
    {
        const double Start = FMath::Lerp(AlongStart, AlongEnd, double(Panel) / Panels);
        const double End = FMath::Lerp(AlongStart, AlongEnd, double(Panel + 1) / Panels);
        FRandomStream Stones(521 + Panel * 173);
        Block(Start, End, 0.0, 24.0, 0.0, ERegimentWallMaterial::Mortar);
        Block(Start, End, 20.0, 171.0, 9.0, ERegimentWallMaterial::Mortar);
        for (int32 Course = 0; Course < 4; ++Course)
        {
            const double Bottom = 23.0 + Course * 36.0;
            const bool Coping = Course == 3;
            const int32 Count = Coping ? 3 : 4;
            double First = Start;
            for (int32 Stone = 0; Stone < Count; ++Stone)
            {
                const double Position = (Stone + 1.0 + Stones.FRandRange(-.28f, .28f)) / Count;
                const double Last = Stone + 1 == Count ? End : FMath::Lerp(Start, End, Position);
                const auto Tint = Coping ? ERegimentWallMaterial::Coping :
                    (Stones.RandRange(0, 3) == 0 ? ERegimentWallMaterial::LightStone : ERegimentWallMaterial::Stone);
                const double Gap = FMath::Min(3.0, (Last - First) * .08);
                Block(First + (Stone == 0 ? 0.0 : Gap), Last - (Stone + 1 == Count ? 0.0 : Gap),
                    Bottom, Bottom + (Coping ? 50.0 : 34.5), Coping ? 0.0 : Stones.FRandRange(1.0f, 6.0f), Tint);
                First = Last;
            }
        }
    }
}
}

UE::Geometry::FDynamicMesh3 BuildRegimentWallMesh(std::span<const battle::Obstacle> Walls)
{
    UE::Geometry::FDynamicMesh3 Mesh;
    Mesh.EnableAttributes();
    Mesh.Attributes()->EnableMaterialID();
    for (const auto& Wall : Walls)
        if (Wall.feature == battle::TerrainFeature::StoneWall)
            AddWall(Mesh, Wall);
    return Mesh;
}
