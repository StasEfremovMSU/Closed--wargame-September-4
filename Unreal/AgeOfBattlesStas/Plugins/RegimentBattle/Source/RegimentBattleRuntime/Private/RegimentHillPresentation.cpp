#include "RegimentHillPresentation.h"

#include "DynamicMesh/DynamicMeshAttributeSet.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Scenario.h"
THIRD_PARTY_INCLUDES_END

namespace
{
using TFeature = battle::TerrainFeature;
using G = ERegimentGroundMaterial;

struct FEarthworkKnot
{
    double Along;
    double RaisedCm;
};

constexpr FEarthworkKnot RampProfile[] = {
    {0.0, 0.0}, {.17, -38.0}, {.35, -38.0}, {.49, -10.0}, {.76, 52.0}, {1.0, 0.0}
};
constexpr FEarthworkKnot BankProfile[] = {
    {0.0, 0.0}, {.28, 155.0}, {.42, 172.0}, {.58, 172.0}, {.73, 150.0}, {1.0, 0.0}
};

TConstArrayView<FEarthworkKnot> EarthworkProfile(TFeature Feature)
{
    return Feature == TFeature::RedoubtRamp ? TConstArrayView<FEarthworkKnot>(RampProfile)
        : TConstArrayView<FEarthworkKnot>(BankProfile);
}

double ProfileHeight(TConstArrayView<FEarthworkKnot> Profile, double Along)
{
    for (int32 Index = 1; Index < Profile.Num(); ++Index)
        if (Along <= Profile[Index].Along)
        {
            const auto& A = Profile[Index - 1];
            const auto& B = Profile[Index];
            return FMath::Lerp(A.RaisedCm, B.RaisedCm, (Along - A.Along) / (B.Along - A.Along));
        }
    return 0.0;
}

bool IsEarthwork(const battle::Obstacle& Area)
{
    return Area.feature == TFeature::RedoubtRamp || Area.feature == TFeature::RedoubtBank;
}

double HillHeightCm(const battle::TerrainHill* Hill, double Xcm, double Ycm)
{
    return Hill ? 100.0 * battle::HillHeight(*Hill, {float(Xcm / 100.0), float(Ycm / 100.0), 0.f}) : 0.0;
}

double EarthworkHeightCm(const battle::Obstacle& Area, double Xcm, double Ycm)
{
    const double MinX = 100.0 * Area.min_x, MaxX = 100.0 * Area.max_x;
    const double MinY = 100.0 * Area.min_y, MaxY = 100.0 * Area.max_y;
    if (Xcm <= MinX || Xcm >= MaxX || Ycm <= MinY || Ycm >= MaxY)
        return 0.0;
    const double X = (Xcm - MinX) / (MaxX - MinX), Y = (Ycm - MinY) / (MaxY - MinY);
    const bool bRamp = Area.feature == TFeature::RedoubtRamp;
    const double Along = bRamp ? X : Y;
    const double End = bRamp ? Y : X;
    return ProfileHeight(EarthworkProfile(Area.feature), Along) *
        FMath::Min(1.0, FMath::Min(End, 1.0 - End) * 12.5);
}
TArray<double> TerrainAxis(double HalfExtent, double HillRadius, double SampleSpacingCm,
    std::span<const battle::Obstacle> Areas, bool bUseX)
{
    const double SampleExtent = FMath::Min(HalfExtent, HillRadius);
    const int32 HalfCells = FMath::Max(1, FMath::CeilToInt(SampleExtent / SampleSpacingCm));
    TArray<double> Coordinates;
    Coordinates.Reserve(2 * HalfCells + 3);
    if (HalfExtent > SampleExtent)
        Coordinates.Add(-HalfExtent);
    for (int32 Index = -HalfCells; Index <= HalfCells; ++Index)
        Coordinates.Add(SampleExtent * (double(Index) / HalfCells));
    if (HalfExtent > SampleExtent)
        Coordinates.Add(HalfExtent);
    for (const auto& Area : Areas)
    {
        const double Low = FMath::Clamp((bUseX ? Area.min_x : Area.min_y) * 100.0, -HalfExtent, HalfExtent);
        const double High = FMath::Clamp((bUseX ? Area.max_x : Area.max_y) * 100.0, -HalfExtent, HalfExtent);
        Coordinates.Add(Low);
        Coordinates.Add(High);
        if (IsEarthwork(Area))
        {
            if (bUseX == (Area.feature == TFeature::RedoubtRamp))
                for (const auto& Step : EarthworkProfile(Area.feature))
                    Coordinates.Add(FMath::Lerp(Low, High, Step.Along));
            else
                for (const double End : {.08, .92})
                    Coordinates.Add(FMath::Lerp(Low, High, End));
        }
    }
    Coordinates.Sort();
    for (int32 Index = Coordinates.Num() - 1; Index > 0; --Index)
        if (FMath::IsNearlyEqual(Coordinates[Index], Coordinates[Index - 1], .01))
            Coordinates.RemoveAt(Index, 1, EAllowShrinking::No);
    return Coordinates;
}

int32 TerrainMaterial(double Xcm, double Ycm, std::span<const battle::Obstacle> Areas)
{
    const double X = Xcm / 100.0, Y = Ycm / 100.0;
    for (const auto& Area : Areas)
    {
        if (X < Area.min_x || X > Area.max_x || Y < Area.min_y || Y > Area.max_y)
            continue;
        switch (Area.feature)
        {
        case TFeature::RedoubtRamp:
            return int32(X < FMath::Lerp(Area.min_x, Area.max_x, .49f) ? G::Ditch : G::EarthBank);
        case TFeature::RedoubtBank:
        {
            const double Across = (Y - Area.min_y) / (Area.max_y - Area.min_y);
            return int32(Across > .28 && Across < .72 ? G::BankTurf : G::EarthBank);
        }
        case battle::TerrainFeature::PloughedField: return int32(ERegimentGroundMaterial::Field);
        case battle::TerrainFeature::RedoubtInterior: return int32(ERegimentGroundMaterial::Field);
        case battle::TerrainFeature::Forest: return int32(ERegimentGroundMaterial::Wood);
        case battle::TerrainFeature::River: return int32(ERegimentGroundMaterial::Water);
        case battle::TerrainFeature::Ford: return int32(ERegimentGroundMaterial::Ford);
        case battle::TerrainFeature::Marsh: return int32(ERegimentGroundMaterial::Marsh);
        default: break;
        }
    }
    return int32(G::Meadow);
}
}

UE::Geometry::FDynamicMesh3 BuildRegimentHillMesh(const battle::TerrainHill& Hill, double HalfWidthCm,
    double HalfDepthCm, std::span<const battle::Obstacle> Areas)
{
    return BuildRegimentGroundMesh(&Hill, HalfWidthCm, HalfDepthCm, Areas);
}

UE::Geometry::FDynamicMesh3 BuildRegimentGroundMesh(const battle::TerrainHill* Hill, double HalfWidthCm,
    double HalfDepthCm, std::span<const battle::Obstacle> Areas)
{
    using namespace UE::Geometry;
    FDynamicMesh3 Mesh;
    Mesh.EnableAttributes();
    Mesh.Attributes()->EnableMaterialID();
    if ((Hill && !battle::ValidHill(*Hill)) || HalfWidthCm <= 0.0 || HalfDepthCm <= 0.0)
        return Mesh;

    const double RadiusX = Hill ? Hill->radius_x_m * 100.0 : HalfWidthCm;
    const double RadiusY = Hill ? Hill->radius_y_m * 100.0 : HalfDepthCm;
    const double MinimumSpacing = Hill && Hill->relief == battle::TerrainRelief::CentralHill ? 90.0 : 380.0;
    const double Spacing = Hill ? FMath::Max(MinimumSpacing, FMath::Max(RadiusX, RadiusY) / 65.0) : 3000.0;
    const TArray<double> X = TerrainAxis(HalfWidthCm, RadiusX, Spacing, Areas, true);
    const TArray<double> Y = TerrainAxis(HalfDepthCm, RadiusY, Spacing, Areas, false);
    auto* UVs = Mesh.Attributes()->PrimaryUV();
    auto* Normals = Mesh.Attributes()->PrimaryNormals();
    TArray<int32> VertexUvs;
    TArray<int32> VertexNormals;
    TArray<const battle::Obstacle*> Earthworks;
    for (const auto& Area : Areas)
        if (IsEarthwork(Area))
            Earthworks.Add(&Area);
    const auto Height = [&](double Xcm, double Ycm)
    {
        double HeightCm = HillHeightCm(Hill, Xcm, Ycm);
        for (const auto* Area : Earthworks)
            HeightCm += EarthworkHeightCm(*Area, Xcm, Ycm);
        return HeightCm;
    };
    const auto AddVertex = [&](double Xcm, double Ycm, double SurfaceOffset = 0.0)
    {
        constexpr double DeltaCm = 20.0;
        const double DzDx = (Height(Xcm + DeltaCm, Ycm) - Height(Xcm - DeltaCm, Ycm)) / (2 * DeltaCm);
        const double DzDy = (Height(Xcm, Ycm + DeltaCm) - Height(Xcm, Ycm - DeltaCm)) / (2 * DeltaCm);
        VertexUvs.Add(UVs->AppendElement(FVector2f(float(Xcm / 1000.0), float(Ycm / 1000.0))));
        VertexNormals.Add(Normals->AppendElement(FVector3f(FVector3d(-DzDx, -DzDy, 1.0).GetSafeNormal())));
        return Mesh.AppendVertex(FVector3d(Xcm, Ycm, Height(Xcm, Ycm) + SurfaceOffset));
    };
    const auto AddTriangle = [&](int32 A, int32 B, int32 C, int32 Material = 0)
    {
        const int32 Triangle = Mesh.AppendTriangle(A, C, B);
        if (Triangle >= 0)
        {
            Mesh.Attributes()->GetMaterialID()->SetValue(Triangle, Material);
            UVs->SetTriangle(Triangle, FIndex3i(VertexUvs[A], VertexUvs[C], VertexUvs[B]));
            Normals->SetTriangle(Triangle, FIndex3i(VertexNormals[A], VertexNormals[C], VertexNormals[B]));
        }
    };
    for (const double Ycm : Y)
        for (const double Xcm : X)
            AddVertex(Xcm, Ycm);
    for (int32 Row = 0; Row + 1 < Y.Num(); ++Row)
        for (int32 Column = 0; Column + 1 < X.Num(); ++Column)
        {
            const int32 LowerLeft = Row * X.Num() + Column;
            const int32 UpperLeft = LowerLeft + X.Num();
            const int32 Material = TerrainMaterial(.5 * (X[Column] + X[Column + 1]),
                .5 * (Y[Row] + Y[Row + 1]), Areas);
            AddTriangle(LowerLeft, LowerLeft + 1, UpperLeft + 1, Material);
            AddTriangle(LowerLeft, UpperLeft + 1, UpperLeft, Material);
        }

    // Mark the objective independently of the ground tessellation. The surface
    // underneath stays continuous and the outline follows the gameplay ellipse.
    constexpr double TrimHalfWidthCm = 21.0;
    constexpr double TrimOffsetCm = 5.0;
    constexpr int32 Segments = 128;
    if (!Hill)
        return Mesh;
    const auto Crest = battle::HillObjectiveCrest(*Hill);
    if (Crest && Crest->radius_x_m * 100.0 > TrimHalfWidthCm &&
        Crest->radius_y_m * 100.0 > TrimHalfWidthCm &&
        (FMath::Abs(Crest->center.x) + Crest->radius_x_m) * 100.0 + TrimHalfWidthCm <= HalfWidthCm &&
        (FMath::Abs(Crest->center.y) + Crest->radius_y_m) * 100.0 + TrimHalfWidthCm <= HalfDepthCm)
    {
        const int32 First = Mesh.VertexCount();
        for (int32 Index = 0; Index < Segments; ++Index)
        {
            const double Angle = 2.0 * UE_DOUBLE_PI * Index / Segments;
            const double CenterX = 100.0 * Crest->center.x, CenterY = 100.0 * Crest->center.y;
            const double Rx = 100.0 * Crest->radius_x_m, Ry = 100.0 * Crest->radius_y_m;
            const double Cos = FMath::Cos(Angle), Sin = FMath::Sin(Angle);
            AddVertex(CenterX + (Rx - TrimHalfWidthCm) * Cos,
                CenterY + (Ry - TrimHalfWidthCm) * Sin, TrimOffsetCm);
            AddVertex(CenterX + (Rx + TrimHalfWidthCm) * Cos,
                CenterY + (Ry + TrimHalfWidthCm) * Sin, TrimOffsetCm);
        }
        for (int32 Index = 0; Index < Segments; ++Index)
        {
            const int32 Here = First + 2 * Index;
            const int32 Next = First + 2 * ((Index + 1) % Segments);
            AddTriangle(Here, Here + 1, Next + 1, int32(ERegimentGroundMaterial::Crest));
            AddTriangle(Here, Next + 1, Next, int32(ERegimentGroundMaterial::Crest));
        }
    }
    return Mesh;
}

UE::Geometry::FDynamicMesh3 BuildRegimentRoadMesh(const battle::TerrainHill* Hill, double HalfWidthCm,
    double HalfDepthCm, std::span<const battle::Obstacle> Areas)
{
    using namespace UE::Geometry;
    FDynamicMesh3 Mesh;
    Mesh.EnableAttributes();
    Mesh.Attributes()->EnableMaterialID();
    auto* Normals = Mesh.Attributes()->PrimaryNormals();
    auto* UVs = Mesh.Attributes()->PrimaryUV();
    constexpr double Strips[] = {-.98, -.62, -.47, -.27, .27, .47, .62, .98};
    int32 RoadIndex = 0;
    for (const auto& Road : Areas)
    {
        if (Road.feature != TFeature::Road)
            continue;
        const FVector2D Min(FMath::Max(-HalfWidthCm, 100.0 * Road.min_x),
            FMath::Max(-HalfDepthCm, 100.0 * Road.min_y));
        const FVector2D Max(FMath::Min(HalfWidthCm, 100.0 * Road.max_x),
            FMath::Min(HalfDepthCm, 100.0 * Road.max_y));
        const FVector2D Size = Max - Min;
        if (Size.X <= 0.0 || Size.Y <= 0.0)
            continue;

        const bool bAlongX = Size.X >= Size.Y;
        const double Length = bAlongX ? Size.X : Size.Y;
        const double HalfWidth = (bAlongX ? Size.Y : Size.X) * .5;
        const int32 Sections = FMath::Clamp(FMath::CeilToInt(Length / 220.0), 2, 420);
        const double Lift = 9.0 + 4.0 * RoadIndex++;
        const int32 First = Mesh.VertexCount();
        for (int32 Section = 0; Section <= Sections; ++Section)
        {
            const double T = double(Section) / Sections;
            const double Along = T * Length;
            const double Drift = .085 * FMath::Sin(Along * .0057) + .045 * FMath::Sin(Along * .012);
            const double Shoulder = .86 + .065 * FMath::Sin(Along * .009 + RoadIndex);
            for (const double Strip : Strips)
            {
                const double Across = HalfWidth * (1.0 + FMath::Clamp(Drift + Shoulder * Strip, -1.0, 1.0));
                const double X = Min.X + (bAlongX ? Along : Across);
                const double Y = Min.Y + (bAlongX ? Across : Along);
                Mesh.AppendVertex(FVector(X, Y, HillHeightCm(Hill, X, Y) + Lift));
                UVs->AppendElement(FVector2f(float(X / 1000.0), float(Y / 1000.0)));
                const double DzDx = (HillHeightCm(Hill, X + 20.0, Y) - HillHeightCm(Hill, X - 20.0, Y)) / 40.0;
                const double DzDy = (HillHeightCm(Hill, X, Y + 20.0) - HillHeightCm(Hill, X, Y - 20.0)) / 40.0;
                Normals->AppendElement(FVector3f(FVector(-DzDx, -DzDy, 1.0).GetSafeNormal()));
            }
        }
        const auto Triangle = [&](int32 A, int32 B, int32 C, int32 Material)
        {
            if (bAlongX)
                Swap(B, C);
            const int32 Id = Mesh.AppendTriangle(A, B, C);
            Mesh.Attributes()->GetMaterialID()->SetValue(Id, Material);
            Normals->SetTriangle(Id, FIndex3i(A, B, C));
            UVs->SetTriangle(Id, FIndex3i(A, B, C));
        };
        for (int32 Section = 0; Section < Sections; ++Section)
            for (int32 Strip = 0; Strip + 1 < UE_ARRAY_COUNT(Strips); ++Strip)
            {
                const int32 A = First + Section * UE_ARRAY_COUNT(Strips) + Strip;
                const int32 B = A + UE_ARRAY_COUNT(Strips);
                const int32 Material = Strip == 2 || Strip == 4 ? 1 : 0;
                Triangle(A, B, B + 1, Material);
                Triangle(A, B + 1, A + 1, Material);
            }
    }
    return Mesh;
}
