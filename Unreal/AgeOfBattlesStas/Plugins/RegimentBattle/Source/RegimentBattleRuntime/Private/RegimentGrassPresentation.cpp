#include "RegimentGrassPresentation.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

namespace
{
constexpr int32 MaxClumps = 12000;
constexpr double MinSpacingCm = 240.0;
constexpr double EdgeClearanceCm = 85.0;

bool ClearOfFieldFeatures(double X, double Y, std::span<const battle::Obstacle> Areas)
{
    for (const auto& Area : Areas)
    {
        if (Area.feature == battle::TerrainFeature::Forest)
            continue;
        if (X >= 100.0 * Area.min_x - EdgeClearanceCm && X <= 100.0 * Area.max_x + EdgeClearanceCm &&
            Y >= 100.0 * Area.min_y - EdgeClearanceCm && Y <= 100.0 * Area.max_y + EdgeClearanceCm)
            return false;
    }
    return true;
}

struct FBlade
{
    FVector2f Root;
    FVector2f Lean;
    float HalfWidth;
    float Height;
};
}

TArray<FTransform> BuildRegimentGrassTransforms(battle::MarchTerrain Terrain, battle::MarchField Field,
    const battle::TerrainHill* Hill, std::span<const battle::Obstacle> Areas)
{
    using T = battle::MarchTerrain;
    if (Terrain == T::Peipus || Terrain == T::Narva)
        return {};

    const double HalfWidth = 100.0 * Field.HalfWidth() - EdgeClearanceCm;
    const double HalfDepth = 100.0 * Field.HalfHeight() - EdgeClearanceCm;
    if (HalfWidth <= 0.0 || HalfDepth <= 0.0)
        return {};

    const double Spacing = FMath::Max(MinSpacingCm, FMath::Sqrt(4.0 * HalfWidth * HalfDepth / MaxClumps));
    const int32 Columns = FMath::Clamp(FMath::FloorToInt(2.0 * HalfWidth / Spacing), 1, MaxClumps);
    const int32 Rows = FMath::Clamp(FMath::FloorToInt(2.0 * HalfDepth / Spacing), 1, MaxClumps / Columns);
    const double CellWidth = 2.0 * HalfWidth / Columns;
    const double CellDepth = 2.0 * HalfDepth / Rows;
    FRandomStream Random(354682);
    TArray<FTransform> Instances;
    Instances.Reserve(Columns * Rows);
    for (int32 Row = 0; Row < Rows; ++Row)
    {
        for (int32 Column = 0; Column < Columns; ++Column)
        {
            const double X = -HalfWidth + (Column + Random.FRandRange(.08f, .92f)) * CellWidth;
            const double Y = -HalfDepth + (Row + Random.FRandRange(.08f, .92f)) * CellDepth;
            const FRotator Turn(0, Random.FRandRange(0.f, 360.f), 0);
            const float Spread = Random.FRandRange(.74f, 1.15f);
            const float Height = Random.FRandRange(.62f, 1.19f);
            if (!ClearOfFieldFeatures(X, Y, Areas))
                continue;

            const double Ground = Hill ? 100.0 * battle::HillHeight(*Hill, {float(X / 100), float(Y / 100), 0.f}) : 0.0;
            // Bury the base slightly to cover the difference between the hill and its rendered triangles.
            Instances.Emplace(Turn, FVector(X, Y, Ground - 7.0), FVector(Spread, Spread, Height));
        }
    }
    return Instances;
}

UStaticMesh* MakeRegimentGrassMesh(UObject& Owner, UMaterialInterface& Material)
{
    const FBlade Blades[] = {
        {{-28, -20}, {-18, -13}, 5, 31}, {{-31, -17}, {9, -16}, 6, 38},
        {{-26, -26}, {-19, 9}, 4, 27}, {{-18, -16}, {1, 18}, 4, 26},
        {{22, 17}, {-6, 23}, 5, 39}, {{27, 21}, {19, 13}, 6, 29},
        {{25, 13}, {15, -11}, 4, 32}, {{17, 16}, {-17, 3}, 4, 24}
    };
    FMeshDescription Description;
    FStaticMeshAttributes Attributes(Description);
    Attributes.Register();
    auto Positions = Attributes.GetVertexPositions();
    auto Normals = Attributes.GetVertexInstanceNormals();
    auto Tangents = Attributes.GetVertexInstanceTangents();
    auto BinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
    Attributes.GetVertexInstanceUVs().SetNumChannels(1);
    const FPolygonGroupID MaterialGroup = Description.CreatePolygonGroup();
    const FName MaterialName(TEXT("Grass"));
    Attributes.GetPolygonGroupMaterialSlotNames()[MaterialGroup] = MaterialName;
    for (const FBlade& Blade : Blades)
    {
        const FVector2f Across = FVector2f(-Blade.Lean.Y, Blade.Lean.X).GetSafeNormal();
        const FVector2f Left = Blade.Root - Across * Blade.HalfWidth;
        const FVector2f Right = Blade.Root + Across * Blade.HalfWidth;
        const FVector2f Tip = Blade.Root + Blade.Lean;
        const FVector3f Points[] = {{Left.X, Left.Y, 0}, {Right.X, Right.Y, 0}, {Tip.X, Tip.Y, Blade.Height}};
        FVertexID Vertices[3];
        for (int32 Index = 0; Index < 3; ++Index)
        {
            Vertices[Index] = Description.CreateVertex();
            Positions[Vertices[Index]] = Points[Index];
        }
        // Both faces use an up-facing normal to give the unshadowed blades the field's diffuse daylight.
        for (const bool bReverse : {false, true})
        {
            const FVertexInstanceID Corners[] = {
                Description.CreateVertexInstance(Vertices[bReverse ? 1 : 0]),
                Description.CreateVertexInstance(Vertices[bReverse ? 0 : 1]),
                Description.CreateVertexInstance(Vertices[2])
            };
            for (const FVertexInstanceID Corner : Corners)
            {
                Normals[Corner] = FVector3f::UpVector;
                Tangents[Corner] = FVector3f(Across.X, Across.Y, 0);
                BinormalSigns[Corner] = 1.f;
            }
            Description.CreateTriangle(MaterialGroup, Corners);
        }
    }

    auto* Mesh = NewObject<UStaticMesh>(&Owner, NAME_None, RF_Transient);
    Mesh->bSupportRayTracing = false;
    Mesh->GetStaticMaterials().Emplace(&Material, MaterialName);
    UStaticMesh::FBuildMeshDescriptionsParams Params;
    Params.bFastBuild = true;
    Params.bCommitMeshDescription = false;
    Params.bMarkPackageDirty = false;
    return Mesh->BuildFromMeshDescriptions({&Description}, Params) ? Mesh : nullptr;
}
