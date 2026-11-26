#include "RegimentMarchBoard.h"
#include "RegimentGrassPresentation.h"
#include "RegimentHillPresentation.h"
#include "RegimentMissionRoster.h"
#include "RegimentWallPresentation.h"

#include "Algo/BinarySearch.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/Level.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif
#include "UObject/StrongObjectPtr.h"

THIRD_PARTY_INCLUDES_START
#include "battle/MarchMission.h"
THIRD_PARTY_INCLUDES_END

namespace
{
constexpr double Cm = 100.0;
constexpr double DefaultViewDistance = 21100.0;
constexpr double RiverHalfWidth = 100.0 * battle::MarchRiverHalfWidth;
constexpr double CrossingHalfWidth = 100.0 * battle::MarchCrossings[0].half_width;

enum class EShape : uint8 { Box, Cylinder, Cone, Sphere, Count };

enum class EBoardColor : uint8
{
    Earth, Rim, Meadow, Lawn, DryGrass, Bank, DeepWater, Ford,
    Stone, Dust, Bridge, Plank, LightPlank, Timber, PineShade, Pine, PineLight,
    Blue, BlueLight, Orange, OrangeLight, Ivory, Brass,
    Harvest, Stubble, WallMortar, WallStone, WallPale, WallCap,
    Plain, Reed, Snow, ShelteredSnow, Ice, IceBand, IceStreak, RouteOpen, RouteBlocked,
    DistantGrass, DistantField, DistantTrees, WinterTrees, Rut, WetGravel, Ditch, EarthBank, BankTurf, Count
};

const TCHAR* const ShapePaths[] =
{
    TEXT("/Engine/BasicShapes/Cube.Cube"),
    TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
    TEXT("/Engine/BasicShapes/Cone.Cone"),
    TEXT("/Engine/BasicShapes/Sphere.Sphere")
};

constexpr uint32 BoardColors[] =
{
    0x3B372D, 0x776546, 0x8B9C62, 0x839459, 0x96A369, 0xAA9669,
    0x315C75, 0x729D96, 0x827B6E, 0xB69A66,
    0x51402E, 0x996C42, 0xAC8152, 0x654B34, 0x415739, 0x526B41, 0x657C48,
    0x255F99, 0x69AEE0, 0xA8472A, 0xEC9663, 0xF3E5C3, 0xF0C46E,
    0xB7A371, 0xC4AE73, 0x645D55, 0x938F83, 0xAFABA0, 0xC0B7A8,
    0xC2AA70, 0x9D9360, 0xCDD9D5, 0xA9B9B4, 0x93BBC7, 0xAEC9D0, 0xD7E5DF,
    0x8FF4BB, 0xFE6F62, 0x82927F, 0xA49E80, 0x586E60, 0x60736F,
    0x89724A, 0x958974, 0x715941, 0x967551, 0x9A8E5C
};

static_assert(UE_ARRAY_COUNT(ShapePaths) == int32(EShape::Count));
static_assert(UE_ARRAY_COUNT(BoardColors) == int32(EBoardColor::Count));

struct FShapeBatch
{
    EShape Shape;
    EBoardColor Color;
    TArray<FTransform> Instances;
};

struct FBoardVisual
{
    TWeakObjectPtr<USceneComponent> Root;
    TArray<TWeakObjectPtr<UDynamicMeshComponent>> Surfaces;
    TArray<TWeakObjectPtr<UInstancedStaticMeshComponent>> Meshes;
    TWeakObjectPtr<UTextRenderComponent> Label;

    void Destroy()
    {
        if (UTextRenderComponent* Text = Label.Get())
            Text->DestroyComponent();
        Label.Reset();

        for (const auto& Mesh : Surfaces)
            if (UDynamicMeshComponent* Component = Mesh.Get())
                Component->DestroyComponent();
        Surfaces.Reset();

        for (const auto& Mesh : Meshes)
            if (UInstancedStaticMeshComponent* Component = Mesh.Get())
                Component->DestroyComponent();
        Meshes.Reset();

        if (USceneComponent* Component = Root.Get())
            Component->DestroyComponent();
        Root.Reset();
    }
};

struct FRegimentVisual
{
    FBoardVisual Token;
    FBoardVisual Selection;
    FBoardVisual Waypoint;
    int32 Team = 0;

    void Destroy()
    {
        Waypoint.Destroy();
        Selection.Destroy();
        Token.Destroy();
    }
};

void AddShape(TArray<FShapeBatch>& Batches, EShape Shape, EBoardColor Color, const FVector& Position,
    const FVector& Size, const FRotator& Rotation = FRotator::ZeroRotator)
{
    FShapeBatch* Batch = Batches.FindByPredicate([Shape, Color](const FShapeBatch& Candidate)
    {
        return Candidate.Shape == Shape && Candidate.Color == Color;
    });
    if (!Batch)
        Batch = &Batches.Add_GetRef({ Shape, Color, {} });

    Batch->Instances.Emplace(Rotation, Position, Size / 100.0);
}

void AddBox(TArray<FShapeBatch>& Batches, EBoardColor Color, const FVector& Position, const FVector& Size,
    double Yaw = 0.0)
{
    AddShape(Batches, EShape::Box, Color, Position, Size, FRotator(0.0, Yaw, 0.0));
}

void AddBridge(TArray<FShapeBatch>& Batches, double Y)
{
    const double BridgeLength = 2.0 * (RiverHalfWidth + 220.0);
    const double BridgeWidth = 2.0 * CrossingHalfWidth;
    AddBox(Batches, EBoardColor::Bridge, { 0.0, Y, -19.0 }, { BridgeLength, BridgeWidth, 100.0 });
    for (double X : { -380.0, 380.0 })
        AddBox(Batches, EBoardColor::Stone, { X, Y, -99.0 }, { 180.0, BridgeWidth + 70.0, 180.0 });

    constexpr int32 PlankCount = 18;
    const double PlankPitch = BridgeLength / PlankCount;
    for (int32 Index = 0; Index < PlankCount; ++Index)
    {
        const double X = (Index + 0.5) * PlankPitch - BridgeLength / 2.0;
        const EBoardColor Color = Index % 3 == 1 ? EBoardColor::LightPlank : EBoardColor::Plank;
        AddBox(Batches, Color, { X, Y, 40.0 }, { PlankPitch - 5.0, BridgeWidth - 4.0, 18.0 });
    }

    for (const double Edge : { Y - CrossingHalfWidth - 25.0, Y + CrossingHalfWidth + 25.0 })
    {
        AddBox(Batches, EBoardColor::Timber, { 0.0, Edge, 142.0 }, { BridgeLength, 42.0, 38.0 });
        for (const double X : { -645.0, -215.0, 215.0, 645.0 })
            AddBox(Batches, EBoardColor::Timber, { X, Edge, 98.0 }, { 50.0, 50.0, 139.0 });
    }
}

void AddFord(TArray<FShapeBatch>& Batches)
{
    AddBox(Batches, EBoardColor::Ford, { 0.0, 0.0, -34.0 },
        { RiverHalfWidth * 2.0, CrossingHalfWidth * 2.0, 54.0 });
    FRandomStream Gravel(1681);
    for (int32 Index = 0; Index < 34; ++Index)
    {
        const double X = Gravel.FRandRange(-.92f, .92f) * RiverHalfWidth;
        const double Y = Gravel.FRandRange(-.93f, .93f) * CrossingHalfWidth;
        const double Width = Gravel.FRandRange(23.f, 89.f);
        AddShape(Batches, EShape::Cylinder, Index % 5 == 0 ? EBoardColor::Stone : EBoardColor::WetGravel,
            {X, Y, -8.0}, {Width, Width * Gravel.FRandRange(.57f, .87f), Gravel.FRandRange(4.f, 14.f)},
            FRotator(5.0, Gravel.FRandRange(0.f, 180.f), -4.0));
    }
}

void AddTree(TArray<FShapeBatch>& Batches, FRandomStream& Random, const FVector& Ground,
    double Height, bool bConifer, bool bLayered)
{
    const double Crown = Height * Random.FRandRange(.52f, .76f);
    const FRotator Lean(Random.FRandRange(-8.f, 8.f), Random.FRandRange(0.f, 360.f),
        Random.FRandRange(-7.f, 7.f));
    const auto Raised = [&](double Scale) { return Ground + Lean.RotateVector(FVector(0, 0, Height * Scale)); };
    const EBoardColor Options[] = {EBoardColor::PineShade, EBoardColor::Pine, EBoardColor::PineLight};
    const EBoardColor Color = Options[Random.RandRange(0, UE_ARRAY_COUNT(Options) - 1)];
    AddShape(Batches, EShape::Cylinder, EBoardColor::Timber, Raised(.23),
        {.20 * Crown, .20 * Crown, .48 * Height}, Lean);
    if (bLayered)
        AddShape(Batches, EShape::Cone, EBoardColor::PineShade, Raised(.48),
            {Crown, Crown * .84, .62 * Height}, Lean);
    AddShape(Batches, bConifer ? EShape::Cone : EShape::Sphere, Color, Raised(bLayered ? .74 : .69),
        {Crown * (bLayered ? .78 : 1.0), Crown * Random.FRandRange(.72f, .94f), .64 * Height}, Lean);
}

void AddWoodland(TArray<FShapeBatch>& Batches, battle::MarchField Field, const battle::TerrainHill* Hill = nullptr)
{
    const FVector2D Groves[] =
    {
        { -5020.0, -2970.0 }, { -2550.0, -3040.0 }, { -4400.0, 2790.0 }, { -1890.0, 3070.0 },
        { 1920.0, -3010.0 }, { 4620.0, -2850.0 }, { 2710.0, 2940.0 }, { 5130.0, 2850.0 }
    };

    const double HorizontalScale = Field.HalfWidth() / battle::MarchHalfWidth;
    const double VerticalScale = Field.HalfHeight() / battle::MarchHalfHeight;
    FRandomStream Random(7027);
    for (const FVector2D& Grove : Groves)
    {
        for (int32 Index = 0; Index < 6; ++Index)
        {
            const double X = Grove.X * HorizontalScale + Random.FRandRange(-550.0f, 550.0f);
            const double Y = Grove.Y * VerticalScale + Random.FRandRange(-325.0f, 325.0f);
            const double Height = Random.FRandRange(235.0f, 550.0f);
            const double Ground = Hill ? Cm * battle::HillHeight(*Hill, {float(X / Cm), float(Y / Cm), 0.f}) : 0.0;
            AddTree(Batches, Random, {X, Y, Ground}, Height, true, true);
        }
    }
}

TArray<FShapeBatch> MakeRiverTerrain(battle::MarchTerrain Terrain, battle::MarchField Field)
{
    TArray<FShapeBatch> Batches;
    const auto Crossings = battle::MarchCrossingsFor(Terrain);
    const auto NearCrossing = [Crossings](double Y, double Distance)
    {
        return std::ranges::any_of(Crossings, [=](const auto& Crossing)
        {
            return FMath::Abs(Y - 100.0 * Crossing.y) < Distance;
        });
    };
    const double BoardHalfWidth = Cm * Field.HalfWidth();
    const double BoardHalfDepth = Cm * Field.HalfHeight();
    const double Width = 2.0 * BoardHalfWidth;
    const double Depth = 2.0 * BoardHalfDepth;
    const double MeadowWidth = BoardHalfWidth - RiverHalfWidth;
    AddBox(Batches, EBoardColor::Earth, { 0.0, 0.0, -260.0 }, { Width, Depth, 360.0 });
    AddBox(Batches, EBoardColor::Rim, { 0.0, 0.0, -378.0 }, { Width + 95.0, Depth + 95.0, 50.0 });
    AddBox(Batches, EBoardColor::DeepWater, { 0.0, 0.0, -65.0 }, { RiverHalfWidth * 2.0, Depth, 50.0 });

    for (double Side : { -1.0, 1.0 })
    {
        AddBox(Batches, EBoardColor::Meadow, { Side * (BoardHalfWidth + RiverHalfWidth) / 2.0, 0.0, -40.0 },
            { MeadowWidth, Depth, 80.0 });
        AddBox(Batches, EBoardColor::Bank, { Side * (RiverHalfWidth - 38.0), 0.0, -28.0 },
            { 75.0, Depth, 47.0 });

        for (const auto& Opening : Crossings)
        {
            const double Crossing = Opening.y * 100.0;
            // The worn track continues into the meadow beyond its packed surface.
            AddBox(Batches, EBoardColor::Bank, { Side * 1740.0, Crossing + Side * 48.0, 1.0 },
                { 725.0, 350.0, 4.0 }, Side * 7.0);
        }

        for (const double Edge : { -1.0, 1.0 })
        {
            const double MeadowScale = MeadowWidth / (Cm * battle::MarchHalfWidth - RiverHalfWidth);
            const double GrassX = RiverHalfWidth + (3710.0 - RiverHalfWidth) * MeadowScale;
            const double LawnX = RiverHalfWidth + (4020.0 - RiverHalfWidth) * MeadowScale;
            AddBox(Batches, EBoardColor::DryGrass, { Side * GrassX, Edge * (BoardHalfDepth - 280.0), 1.0 },
                { 4550.0 * MeadowScale, 525.0, 3.0 });
            AddBox(Batches, EBoardColor::Lawn, { Side * LawnX, Edge * (BoardHalfDepth - 650.0), 1.5 },
                { 2600.0 * MeadowScale, 470.0, 4.0 }, Side * Edge * 8.0);
        }
    }

    FRandomStream Shore(344);
    const float ShoreLimit = float(BoardHalfDepth - 210.0);
    const int32 StoneCount = FMath::RoundToInt(17.0 * BoardHalfDepth / (Cm * battle::MarchHalfHeight));
    for (int32 Index = 0; Index < StoneCount; ++Index)
    {
        const double Y = Shore.FRandRange(-ShoreLimit, ShoreLimit);
        if (NearCrossing(Y, 500.0))
            continue;
        const double Bank = Index % 2 == 0 ? -RiverHalfWidth : RiverHalfWidth;
        AddShape(Batches, EShape::Sphere, EBoardColor::Stone, {Bank, Y, -13.0},
            {Shore.FRandRange(65.0f, 135.0f), Shore.FRandRange(45.0f, 118.0f), 55.0});
    }

    for (const auto& Opening : Crossings)
    {
        if (Opening.ford)
            AddFord(Batches);
        else
            AddBridge(Batches, Opening.y * 100.0);
    }
    AddWoodland(Batches, Field);
    return Batches;
}

void AddHedge(TArray<FShapeBatch>& Batches, const battle::Obstacle& Hedge)
{
    const double X = (Hedge.min_x + Hedge.max_x) * Cm / 2.0;
    const double Y = (Hedge.min_y + Hedge.max_y) * Cm / 2.0;
    const double Width = (Hedge.max_x - Hedge.min_x) * Cm;
    const double Length = (Hedge.max_y - Hedge.min_y) * Cm;
    const int32 Panels = FMath::Max(1, FMath::CeilToInt(Length / 210.0));
    const double Span = Length / Panels;
    AddBox(Batches, EBoardColor::Timber, { X, Y, 72.0 }, { Width * .62, Length, 104.0 });
    for (int32 Panel = 0; Panel < Panels; ++Panel)
    {
        const double Center = Hedge.min_y * Cm + (Panel + .5) * Span;
        AddBox(Batches, Panel % 2 == 0 ? EBoardColor::Bridge : EBoardColor::Timber,
            { X, Center, 138.0 }, { Width, Span, 28.0 });
        AddBox(Batches, EBoardColor::Bridge, { X, Center, 92.0 }, { Width, FMath::Min(28.0, Span), 184.0 });
        for (const double Side : {-1.0, 1.0})
            AddBox(Batches, EBoardColor::Plank, { X + Side * Width * .34, Center, 41.0 },
                { Width * .28, Span, 28.0 });
    }
}

TArray<FShapeBatch> MakeHistoricTerrain(battle::MarchTerrain Terrain, battle::MarchField Field)
{
    TArray<FShapeBatch> Batches;
    const double BoardHalfWidth = Cm * Field.HalfWidth();
    const double BoardHalfDepth = Cm * Field.HalfHeight();
    const double Width = 2.0 * BoardHalfWidth;
    const double Depth = 2.0 * BoardHalfDepth;
    const bool bCannae = Terrain == battle::MarchTerrain::Cannae;
    AddBox(Batches, EBoardColor::Earth, {0.0, 0.0, -260.0}, {Width, Depth, 360.0});
    if (!bCannae)
        AddBox(Batches, EBoardColor::Snow, {0.0, 0.0, -40.0}, {Width, Depth, 80.0});

    if (bCannae)
    {
        // The river borders the table beyond the playable formation bounds.
        const double BankY = -BoardHalfDepth - 118.0;
        const double RiverY = -BoardHalfDepth - 980.0;
        AddBox(Batches, EBoardColor::Bank, {0.0, BankY, -30.0}, {Width, 236.0, 56.0});
        AddBox(Batches, EBoardColor::DeepWater, {0.0, RiverY, -69.0}, {Width, 1570.0, 65.0});


        FRandomStream Shore(216);
        const float ShoreLimit = float(BoardHalfWidth - 50.0);
        const int32 ShoreCount = FMath::RoundToInt(54.0 * BoardHalfWidth / (Cm * battle::MarchHalfWidth));
        for (int32 Index = 0; Index < ShoreCount; ++Index)
        {
            const double X = Shore.FRandRange(-ShoreLimit, ShoreLimit);
            if (Index % 3 == 0)
                AddShape(Batches, EShape::Cone, EBoardColor::Reed, {X, BankY, 14.0}, {57.0, 42.0, 38.0});
        }
        return Batches;
    }

    // The model uses continuous, walkable ice. Segments describe the shore visually.
    constexpr double Shoreline[] = {530.0, 295.0, 120.0, -20.0, -95.0, 60.0, 268.0, 465.0};
    const double Span = Width / UE_ARRAY_COUNT(Shoreline);
    for (int32 Section = 0; Section < UE_ARRAY_COUNT(Shoreline); ++Section)
    {
        const double X = -BoardHalfWidth + (Section + .5) * Span;
        const double BankY = Shoreline[Section];
        const double IceDepth = BoardHalfDepth - BankY;
        AddBox(Batches, EBoardColor::Ice, {X, BankY + IceDepth * .5, 0.0}, {Span, IceDepth, 2.0});
        AddBox(Batches, EBoardColor::IceBand, {X, BankY, 1.0}, {Span, 133.0, 2.0});
        AddBox(Batches, EBoardColor::ShelteredSnow, {X, BankY - 140.0, .5}, {Span, 145.0, 2.0});
    }
    FRandomStream Ice(1242);
    const float IceLimitX = float(BoardHalfWidth - 370.0);
    const float IceLimitY = float(BoardHalfDepth - 200.0);
    const double IceAreaScale = Width * Depth / (4.0 * Cm * Cm * battle::MarchHalfWidth * battle::MarchHalfHeight);
    const int32 IceStreakCount = FMath::Clamp(FMath::RoundToInt(38.0 * IceAreaScale), 38, 440);
    for (int32 Index = 0; Index < IceStreakCount; ++Index)
    {
        const double X = Ice.FRandRange(-IceLimitX, IceLimitX);
        const double Y = Ice.FRandRange(765.0f, IceLimitY);
        AddBox(Batches, EBoardColor::IceStreak, {X, Y, 1.2},
            {Ice.FRandRange(57.0f, 445.0f), 9.0, 1.0}, Ice.FRandRange(-35.0f, 35.0f));
    }
    // The rock is a landmark outside the playable area.
    AddShape(Batches, EShape::Sphere, EBoardColor::Stone, {-BoardHalfWidth - 270.0, 270.0, 18.0}, {480.0, 420.0, 340.0});
    AddShape(Batches, EShape::Sphere, EBoardColor::Snow, {-BoardHalfWidth - 270.0, 270.0, 173.0}, {340.0, 292.0, 65.0});
    return Batches;
}

TArray<FShapeBatch> MakeDetailedTerrain(battle::MarchTerrain Terrain, battle::MarchField Field)
{
    using T = battle::TerrainFeature;
    TArray<FShapeBatch> Batches;
    const auto Hill = battle::MarchHill(Terrain, Field);
    const auto Areas = battle::MarchTerrainAreas(Terrain, Field);
    const double Width = Cm * Field.width_m, Depth = Cm * Field.height_m;
    // The backing slab sits below the lowest possible depression in the detailed profiles.
    const double Basement = Hill && Hill->relief >= battle::TerrainRelief::HistoricalFirst
        ? -225.0 - Cm * Hill->height_m : -225.0;
    AddBox(Batches, EBoardColor::Earth, {0.0, 0.0, Basement}, {Width, Depth, 300.0});

    const auto Ground = [&](double X, double Y)
    {
        return Hill ? Cm * battle::HillHeight(*Hill, {float(X / Cm), float(Y / Cm), 0.f}) : 0.0;
    };
    FRandomStream Random(8609 + int32(Terrain));
    for (const auto& Area : Areas)
    {
        const double X = Cm * .5 * (Area.min_x + Area.max_x);
        const double Y = Cm * .5 * (Area.min_y + Area.max_y);
        const double SizeX = Cm * (Area.max_x - Area.min_x);
        const double SizeY = Cm * (Area.max_y - Area.min_y);
        const double Z = Ground(X, Y);
        switch (Area.feature)
        {
        case T::Forest:
        {
            const int32 Count = FMath::Clamp(FMath::RoundToInt(SizeX * SizeY / 610000.0), 3, 80);
            for (int32 Index = 0; Index < Count; ++Index)
            {
                const double Tx = X + SizeX * Random.FRandRange(-.46f, .46f);
                const double Ty = Y + SizeY * Random.FRandRange(-.46f, .46f);
                const double Tz = Ground(Tx, Ty);
                const bool bConifer = Terrain == battle::MarchTerrain::Sekigahara ||
                    Terrain == battle::MarchTerrain::Nagashino || Terrain == battle::MarchTerrain::Kawanakajima ||
                    Terrain == battle::MarchTerrain::Mikatagahara || Terrain == battle::MarchTerrain::Okehazama;
                AddTree(Batches, Random, {Tx, Ty, Tz}, Random.FRandRange(310.f, 690.f), bConifer,
                    bConifer && Index % 3 == 0);
            }
            break;
        }
        case T::PloughedField:
        {
            const double RowSpacing = FMath::Max(420.0, SizeY / 18.0);
            constexpr int32 LengthSegments = 6;
            const double Segment = SizeX * .92 / LengthSegments;
            for (int32 Row = 0; (Row + .5) * RowSpacing < SizeY; ++Row)
            {
                const double AlongY = Y - SizeY * .5 + (Row + .5) * RowSpacing;
                const auto Tint = Row % 3 == 0 ? EBoardColor::Harvest : EBoardColor::Rut;
                for (int32 Index = 0; Index < LengthSegments; ++Index)
                {
                    const double AlongX = X + (Index + .5 - LengthSegments * .5) * Segment;
                    const double Base = Ground(AlongX, AlongY);
                    const double Rise = Ground(AlongX + Segment * .5, AlongY) - Ground(AlongX - Segment * .5, AlongY);
                    AddShape(Batches, EShape::Box, Tint, {AlongX, AlongY, Base + 4.0},
                        {Segment * .96, 42.0, 3.0}, FRotator(FMath::RadiansToDegrees(std::atan2(Rise, Segment)), 0, 0));
                }
            }
            break;
        }
        case T::Barrier:
        {
            const double Height = FMath::Clamp(FMath::Min(SizeX, SizeY) * .4, 310.0, 510.0);
            AddBox(Batches, EBoardColor::WallPale, {X, Y, Z + .5 * Height - 35.0}, {SizeX, SizeY, Height + 70.0});
            const double RoofPanel = SizeY * .53;
            for (const double Side : {-1.0, 1.0})
            {
                AddShape(Batches, EShape::Box, EBoardColor::Orange,
                    {X, Y + Side * RoofPanel * .45, Z + Height + RoofPanel * .15},
                    {SizeX + 80.0, RoofPanel + 32.0, 25.0}, FRotator(0.0, 0.0, Side * 17.0));
                const double Face = Y + Side * (SizeY * .5 + 3.0);
                AddBox(Batches, EBoardColor::WallStone, {X, Face, Z + 43.0}, {SizeX, 16.0, 105.0});
                AddBox(Batches, EBoardColor::Timber, {X, Face, Z + Height * .24}, {119.0, 19.0, Height * .49});
                const int32 Windows = FMath::Clamp(FMath::RoundToInt(SizeX / 410.0), 2, 8);
                for (int32 Index = 0; Index < Windows; ++Index)
                {
                    const double SillX = X + ((Index + .5) / Windows - .5) * SizeX * .84;
                    if (FMath::Abs(SillX - X) < 95.0)
                        continue;
                    AddBox(Batches, EBoardColor::Earth, {SillX, Face, Z + Height * .60}, {79.0, 11.0, 91.0});
                    AddBox(Batches, EBoardColor::WallCap, {SillX, Face + Side * 8.0, Z + Height * .60 - 50.0}, {95.0, 19.0, 13.0});
                }
            }
            AddBox(Batches, EBoardColor::WallStone, {X + SizeX * .27, Y, Z + Height + RoofPanel * .22},
                {105.0, 97.0, 182.0});
            break;
        }
        case T::StoneWall:
        {
            const bool bRedoubt = Terrain == battle::MarchTerrain::Poltava ||
                Terrain == battle::MarchTerrain::Narva || Terrain == battle::MarchTerrain::Borodino;
            const double Height = bRedoubt ? 145.0 : 245.0;
            AddBox(Batches, bRedoubt ? EBoardColor::Bank : EBoardColor::WallStone,
                {X, Y, Z + .5 * Height}, {SizeX, SizeY, Height});
            AddBox(Batches, bRedoubt ? EBoardColor::Earth : EBoardColor::WallCap,
                {X, Y, Z + Height}, {SizeX * (bRedoubt ? .7 : 1.04), SizeY * (bRedoubt ? .7 : 1.04), 42.0});
            break;
        }
        case T::Hedge:
            AddBox(Batches, EBoardColor::Timber, {X, Y, Z + 126.0}, {SizeX, SizeY, 118.0});
            for (double Post = -SizeY * .5 + 75.0; Post < SizeY * .5; Post += 210.0)
                AddBox(Batches, EBoardColor::Plank, {X, Y + Post, Z + 105.0}, {SizeX, 38.0, 210.0});
            break;
        default:
            break;
        }
    }
    return Batches;
}

struct FSurroundingLandscape
{
    UE::Geometry::FDynamicMesh3 Ground;
    TArray<EBoardColor> Materials;
    TArray<FShapeBatch> Groves;
    TArray<FShapeBatch> Landmarks;
};

constexpr double LandscapeReach = 280000.0;

EBoardColor ExteriorFieldColor(battle::MarchTerrain Terrain)
{
    using T = battle::MarchTerrain;
    if (Terrain == T::Peipus)
        return EBoardColor::Snow;
    if (Terrain == T::Cannae)
        return EBoardColor::Plain;
    if (Terrain == T::OpenField)
        return EBoardColor::Harvest;
    return EBoardColor::Meadow;
}

bool HasDrySurroundings(battle::MarchTerrain Terrain)
{
    using T = battle::MarchTerrain;
    return Terrain == battle::MarchTerrain::Thermopylae || Terrain == battle::MarchTerrain::Issus ||
        Terrain == battle::MarchTerrain::Hydaspes || Terrain == battle::MarchTerrain::Chaeronea ||
        Terrain == battle::MarchTerrain::OpenField || Terrain == battle::MarchTerrain::Cannae || Terrain == T::Gaugamela || Terrain == T::Zama ||
        Terrain == T::Kalka || Terrain == T::Dorylaeum;
}

bool HasWinterSurroundings(battle::MarchTerrain Terrain)
{
    return Terrain == battle::MarchTerrain::Narva || Terrain == battle::MarchTerrain::Peipus ||
        Terrain == battle::MarchTerrain::Fraustadt;
}

template <typename FSurfaceHeight>
TArray<FShapeBatch> MakeExteriorLandmark(battle::MarchTerrain Terrain, FVector2D HalfExtent, FSurfaceHeight HeightAt)
{
    using T = battle::MarchTerrain;
    using B = EBoardColor;
    TArray<FShapeBatch> Batches;
    if (Terrain == T::OpenField)
    {
        // Low limestone outcrop at the entrance to the dry valley.
        const FVector2D Center(.31 * HalfExtent.X, -HalfExtent.Y - 2420.0);
        for (const double Offset : {-620.0, -160.0, 350.0})
        {
            const FVector2D P = Center + FVector2D(Offset, .17 * Offset);
            const double RockHeight = 345.0 + .40 * FMath::Abs(Offset);
            const double Z = HeightAt(P);
            AddBox(Batches, B::WallPale, {P.X, P.Y, Z + .28 * RockHeight}, {690, 780, RockHeight}, 12.0 + .02 * Offset);
            AddBox(Batches, B::WallStone, {P.X - 106, P.Y + 24, Z + .65 * RockHeight}, {475, 610, .39 * RockHeight}, -16.0);
        }
    }
    else if (Terrain == T::Waterloo)
    {
        // A small steeple and church roof give the surrounding farmland a stable bearing.
        const FVector2D P(-.43 * HalfExtent.X, HalfExtent.Y + 2550.0);
        const double Z = HeightAt(P);
        AddBox(Batches, B::WallPale, {P.X, P.Y, Z + 275.0}, {1170, 625, 590});
        AddBox(Batches, B::WallStone, {P.X - 540, P.Y, Z + 465.0}, {335, 350, 960});
        AddShape(Batches, EShape::Cone, B::Bridge, {P.X - 540, P.Y, Z + 1135.0}, {390, 390, 450});
        for (const double Side : {-1.0, 1.0})
            AddShape(Batches, EShape::Box, B::Bridge, {P.X + 35, P.Y + Side * 151, Z + 634.0},
                {1120, 351, 32}, FRotator(0.0, 0.0, Side * 25.0));
    }
    else if (Terrain == T::Narva)
    {
        const FVector2D P(-HalfExtent.X - 2520.0, .36 * HalfExtent.Y);
        const double Z = HeightAt(P);
        AddBox(Batches, B::WallStone, {P.X, P.Y, Z + 409.0}, {630, 680, 870});
        AddBox(Batches, B::ShelteredSnow, {P.X, P.Y, Z + 838.0}, {695, 741, 96});
        AddShape(Batches, EShape::Cone, B::Bridge, {P.X, P.Y, Z + 1040.0}, {740, 780, 440});
        AddBox(Batches, B::Timber, {P.X, P.Y + 340.0, Z + 543.0}, {120, 22, 180});
    }
    return Batches;
}

TArray<double> ExteriorAxis(double Half, TArray<double> EdgeSamples, TConstArrayView<double> AdditionalSamples)
{
    if (EdgeSamples.IsEmpty())
        for (int32 Index = -8; Index <= 8; ++Index)
            EdgeSamples.Add(Half * (Index / 8.0));
    EdgeSamples.Append(AdditionalSamples.GetData(), AdditionalSamples.Num());
    for (const double Offset : {1300., 4200., 8500., 15500., 27000., 48000., 90000., 160000., LandscapeReach})
    {
        EdgeSamples.Add(-Half - Offset);
        EdgeSamples.Add(Half + Offset);
    }
    EdgeSamples.Sort();
    for (int32 Index = EdgeSamples.Num() - 1; Index > 0; --Index)
        if (FMath::Abs(EdgeSamples[Index] - EdgeSamples[Index - 1]) < .001)
            EdgeSamples.RemoveAt(Index, 1, EAllowShrinking::No);
    return EdgeSamples;
}

FSurroundingLandscape MakeSurroundingLandscape(battle::MarchTerrain Terrain, FVector2D HalfExtent,
    const UE::Geometry::FDynamicMesh3* Core, std::span<const battle::Obstacle> Areas)
{
    using namespace UE::Geometry;
    using T = battle::MarchTerrain;
    FSurroundingLandscape Result;
    FDynamicMesh3& Mesh = Result.Ground;
    Mesh.EnableAttributes();
    Mesh.Attributes()->EnableMaterialID();
    auto* UVs = Mesh.Attributes()->PrimaryUV();
    auto* Normals = Mesh.Attributes()->PrimaryNormals();
    auto* MaterialIds = Mesh.Attributes()->GetMaterialID();

    // Reuse the rendered hill edge samples so the exterior meets the mesh at its exact elevations.
    TArray<double> InnerX, InnerY;
    TMap<FVector2D, double> Rim;
    if (Core)
    {
        for (const int32 Id : Core->VertexIndicesItr())
        {
            const FVector Vertex = Core->GetVertex(Id);
            if (FMath::Abs(Vertex.X) > HalfExtent.X || FMath::Abs(Vertex.Y) > HalfExtent.Y)
                continue;
            const bool bSideX = FMath::Abs(Vertex.X) == HalfExtent.X;
            const bool bSideY = FMath::Abs(Vertex.Y) == HalfExtent.Y;
            if (bSideX)
                InnerY.Add(Vertex.Y);
            if (bSideY)
                InnerX.Add(Vertex.X);
            if (bSideX || bSideY)
                Rim.Add(FVector2D(Vertex.X, Vertex.Y), Vertex.Z);
        }
    }
    const bool bChannel = Terrain == T::River || Terrain == T::StirlingBridge || Terrain == T::Granicus;
    const bool bCoast = Terrain == T::Thermopylae || Terrain == T::Issus;
    const double ChannelSamples[] = {-RiverHalfWidth, -RiverHalfWidth + 90.0,
        RiverHalfWidth - 90.0, RiverHalfWidth};
    const double CannaeSamples[] = {-HalfExtent.Y - 1765.0, -HalfExtent.Y - 235.0};
    TConstArrayView<double> WaterEdgeSamples;
    double NarvaSamples[2] = {};
    if (Terrain == T::Cannae)
        WaterEdgeSamples = CannaeSamples;
    else if (Terrain == T::Narva)
        for (const auto& Area : Areas)
            if (Area.feature == battle::TerrainFeature::River && Cm * Area.max_x >= HalfExtent.X)
            {
                NarvaSamples[0] = Cm * Area.min_y;
                NarvaSamples[1] = Cm * Area.max_y;
                WaterEdgeSamples = NarvaSamples;
                break;
            }
    const TArray<double> XAxis = ExteriorAxis(HalfExtent.X, MoveTemp(InnerX),
        bChannel ? TConstArrayView<double>(ChannelSamples) : TConstArrayView<double>());
    const TArray<double> YAxis = ExteriorAxis(HalfExtent.Y, MoveTemp(InnerY), WaterEdgeSamples);

    const auto ExteriorDistance = [HalfExtent](FVector2D Point)
    {
        return FMath::Max(FMath::Abs(Point.X) - HalfExtent.X, FMath::Abs(Point.Y) - HalfExtent.Y);
    };
    const auto IsWater = [=](FVector2D Point)
    {
        if (bChannel)
            return FMath::Abs(Point.Y) >= HalfExtent.Y && FMath::Abs(Point.X) < RiverHalfWidth;
        if (bCoast)
            return Point.Y < -HalfExtent.Y;
        if (Terrain == T::Cannae)
            return Point.Y < -HalfExtent.Y - 235.0 && Point.Y > -HalfExtent.Y - 1765.0;
        if (Terrain == T::Narva)
            return !WaterEdgeSamples.IsEmpty() && Point.X >= HalfExtent.X &&
                Point.Y >= WaterEdgeSamples[0] - .001 && Point.Y <= WaterEdgeSamples[1] + .001;
        return false;
    };
    const bool bSnow = HasWinterSurroundings(Terrain);
    const bool bDry = HasDrySurroundings(Terrain);
    const auto GroundHeight = [&](FVector2D Point)
    {
        const double Distance = ExteriorDistance(Point);
        const FVector2D PlayablePoint(FMath::Clamp(Point.X, -HalfExtent.X, HalfExtent.X),
            FMath::Clamp(Point.Y, -HalfExtent.Y, HalfExtent.Y));
        const double EdgeHeight = Rim.FindRef(PlayablePoint);
        if (Terrain == T::Narva && IsWater(Point))
            return 0.0;
        if (Terrain == T::Cannae && Point.Y <= -HalfExtent.Y - 235.0 && Point.Y >= -HalfExtent.Y - 1765.0)
            return -42.0;
        if (bChannel && FMath::Abs(Point.X) < RiverHalfWidth && FMath::Abs(Point.Y) >= HalfExtent.Y)
            return -40.0;
        if (bCoast && Point.Y <= -HalfExtent.Y)
            return EdgeHeight * FMath::Exp(-Distance / 4200.0) - 30.0 * FMath::Min(1.0, Distance / 4200.0);
        const double Undulation = FMath::PerlinNoise2D(Point / 38200.0 + FVector2D(3.7, 5.8)) +
            .36 * FMath::PerlinNoise2D(Point / 12500.0 + FVector2D(19.1, -4.5));
        const double Relief = Undulation * (bDry ? 5800.0 : 8400.0);
        return EdgeHeight * (1.0 - FMath::SmoothStep(0., 4200., Distance)) +
            (Relief - 240.0) * FMath::SmoothStep(1200., 40000., Distance);
    };
    const auto CellColor = [&](FVector2D Point)
    {
        const double Distance = ExteriorDistance(Point);
        if (IsWater(Point))
            return EBoardColor::DeepWater;
        if (Terrain == T::Peipus && Point.Y > HalfExtent.Y && Point.Y < HalfExtent.Y + 4500.0 &&
            FMath::Abs(Point.X) < HalfExtent.X + 4200.0)
            return EBoardColor::Ice;
        if (Distance < 3100.0)
            return ExteriorFieldColor(Terrain);
        const float Variation = FMath::PerlinNoise2D(Point / 9300.0 + FVector2D(9.3, 6.2));
        if (bSnow)
            return Variation > .19f ? EBoardColor::ShelteredSnow : EBoardColor::Snow;
        if (Distance > 40000.0)
            return Variation > .08f ? EBoardColor::DistantField : EBoardColor::DistantGrass;
        if (bDry)
            return Variation > .13f ? EBoardColor::Harvest : EBoardColor::Plain;
        return Variation > .18f ? EBoardColor::DryGrass : Variation < -.18f ? EBoardColor::Lawn : EBoardColor::Meadow;
    };

    TArray<int32> Vertices;
    Vertices.Init(INDEX_NONE, XAxis.Num() * YAxis.Num());
    for (int32 Row = 0; Row < YAxis.Num(); ++Row)
        for (int32 Column = 0; Column < XAxis.Num(); ++Column)
        {
            const FVector2D Point(XAxis[Column], YAxis[Row]);
            if (ExteriorDistance(Point) >= 0.0)
            {
                const double Height = GroundHeight(Point);
                Vertices[Row * XAxis.Num() + Column] = Mesh.AppendVertex(FVector(Point.X, Point.Y, Height));
                UVs->AppendElement(FVector2f(float(Point.X / 1000.0), float(Point.Y / 1000.0)));
            }
        }
    const auto AddTriangle = [&](int32 A, int32 B, int32 C, int32 Material)
    {
        const int32 Triangle = Mesh.AppendTriangle(A, C, B);
        const FVector P = Mesh.GetVertex(A);
        const FVector3f Normal(FVector::CrossProduct(Mesh.GetVertex(B) - P, Mesh.GetVertex(C) - P).GetSafeNormal());
        UVs->SetTriangle(Triangle, FIndex3i(A, C, B));
        Normals->SetTriangle(Triangle, FIndex3i(
            Normals->AppendElement(Normal), Normals->AppendElement(Normal), Normals->AppendElement(Normal)));
        MaterialIds->SetValue(Triangle, Material);
    };
    for (int32 Row = 0; Row + 1 < YAxis.Num(); ++Row)
        for (int32 Column = 0; Column + 1 < XAxis.Num(); ++Column)
        {
            const FVector2D Center(.5 * (XAxis[Column] + XAxis[Column + 1]), .5 * (YAxis[Row] + YAxis[Row + 1]));
            if (ExteriorDistance(Center) < 0.0)
                continue;
            const int32 Material = Result.Materials.AddUnique(CellColor(Center));
            const int32 Lower = Row * XAxis.Num() + Column;
            const int32 Upper = Lower + XAxis.Num();
            AddTriangle(Vertices[Lower], Vertices[Lower + 1], Vertices[Upper + 1], Material);
            AddTriangle(Vertices[Lower], Vertices[Upper + 1], Vertices[Upper], Material);
        }

    const auto RenderedHeight = [&](const FVector2D& Point)
    {
        const int32 Column = Algo::UpperBound(XAxis, Point.X) - 1;
        const int32 Row = Algo::UpperBound(YAxis, Point.Y) - 1;
        const double U = (Point.X - XAxis[Column]) / (XAxis[Column + 1] - XAxis[Column]);
        const double V = (Point.Y - YAxis[Row]) / (YAxis[Row + 1] - YAxis[Row]);
        const int32 Lower = Row * XAxis.Num() + Column;
        const int32 Upper = Lower + XAxis.Num();
        const double A = Mesh.GetVertex(Vertices[Lower]).Z;
        const double B = Mesh.GetVertex(Vertices[Upper + 1]).Z;
        const double Side = Mesh.GetVertex(Vertices[U >= V ? Lower + 1 : Upper]).Z;
        return A + (B - A) * FMath::Min(U, V) + (Side - A) * FMath::Abs(U - V);
    };
    Result.Landmarks = MakeExteriorLandmark(Terrain, HalfExtent, RenderedHeight);

    const bool bConifers = bSnow || Terrain == T::Sekigahara || Terrain == T::Nagashino || Terrain == T::Kawanakajima;
    const int32 NumberOfGroves = bDry ? 12 : 20;
    FRandomStream Random(21479 + int32(Terrain));
    for (int32 Index = 0; Index < NumberOfGroves; ++Index)
    {
        const double Angle = (Index + Random.FRandRange(.1f, .9f)) * (2.0 * UE_DOUBLE_PI / NumberOfGroves);
        const double Radius = Random.FRandRange(7400.0f, 49000.0f);
        const FVector2D Center(FMath::Cos(Angle) * (HalfExtent.X + Radius),
            FMath::Sin(Angle) * (HalfExtent.Y + Radius));
        for (int32 Tree = 0; Tree < 7; ++Tree)
        {
            const FVector2D Point = Center + FVector2D(Random.FRandRange(-1700.f, 1700.f),
                Random.FRandRange(-1350.f, 1350.f));
            const double Distance = ExteriorDistance(Point);
            if (Distance < 2200.0 || IsWater(Point) || (bSnow && CellColor(Point) == EBoardColor::Ice))
                continue;
            const double Height = Random.FRandRange(410.f, 830.f);
            const double Crown = Height * Random.FRandRange(.53f, .72f);
            const double Root = RenderedHeight(Point);
            const EBoardColor Color = bSnow ? EBoardColor::WinterTrees :
                Distance > 21000.0 ? EBoardColor::DistantTrees : (Tree % 3 == 0 ? EBoardColor::PineLight : EBoardColor::Pine);
            AddShape(Result.Groves, bConifers ? EShape::Cone : EShape::Sphere, Color,
                {Point.X, Point.Y, Root + Height * .34}, {Crown, Crown * Random.FRandRange(.70f, 1.0f), Height * 1.04},
                FRotator(Random.FRandRange(-9.f, 9.f), Random.FRandRange(0.f, 360.f), Random.FRandRange(-7.f, 7.f)));
        }
    }
    return Result;
}

bool IsStockBattleMap(const UWorld& World)
{
    if (!World.PersistentLevel)
        return false;
    return UWorld::StripPIEPrefixFromPackageName(World.PersistentLevel->GetOutermost()->GetName(),
        World.StreamingLevelsPrefix) == TEXT("/Game/Battle/Maps/RegimentField");
}

void ConfigureLandscapePrimitive(UPrimitiveComponent& Component)
{
    Component.SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component.SetCanEverAffectNavigation(false);
    Component.SetCastShadow(false);
    Component.SetAffectDistanceFieldLighting(false);
    Component.SetAffectDynamicIndirectLighting(false);
    Component.SetVisibleInRayTracing(false);
    Component.SetReceivesDecals(false);
}

struct FRegimentSkyProfile
{
    FLinearColor Zenith;
    FLinearColor Horizon;
    FLinearColor CloudShade;
    FLinearColor CloudLight;
    FLinearColor SunGlow;
    FLinearColor CloudLayout;
    float SunPitch;
    float Temperature;
    float SunYawOffset = 0.f;
};

const FRegimentSkyProfile& SkyProfileFor(battle::MarchTerrain Terrain)
{
    using T = battle::MarchTerrain;
    static constexpr FRegimentSkyProfile Temperate{
        {.073f, .24f, .53f}, {.46f, .63f, .78f}, {.38f, .47f, .61f}, {.96f, .99f, 1.06f},
        {.31f, .22f, .11f}, {.42f, .86f, .44f, .53f}, -49.f, 6150.f };
    static constexpr FRegimentSkyProfile Dry{
        {.035f, .20f, .57f}, {.58f, .69f, .71f}, {.51f, .46f, .41f}, {1.04f, .96f, .79f},
        {.48f, .29f, .10f}, {.49f, .72f, .12f, .37f}, -63.f, 5550.f };
    static constexpr FRegimentSkyProfile Winter{
        {.16f, .24f, .36f}, {.49f, .57f, .67f}, {.28f, .35f, .48f}, {.76f, .82f, .91f},
        {.15f, .17f, .22f}, {.26f, .94f, .68f, .46f}, -32.f, 7200.f };
    static constexpr FRegimentSkyProfile Amber{
        {.11f, .16f, .43f}, {.98f, .48f, .28f}, {.30f, .25f, .43f}, {1.04f, .68f, .48f},
        {.82f, .39f, .12f}, {.37f, .82f, .33f, .83f}, -18.f, 4750.f, 180.f };
    if (HasWinterSurroundings(Terrain))
        return Winter;
    if (Terrain == T::Waterloo || Terrain == T::Pavia || Terrain == T::Kawanakajima)
        return Amber;
    return HasDrySurroundings(Terrain) ? Dry : Temperate;
}

struct FRegimentDaylight
{
    TWeakObjectPtr<UDirectionalLightComponent> StockSun;
    TWeakObjectPtr<USkyLightComponent> StockSky;
    TWeakObjectPtr<ADirectionalLight> SpawnedSun;
    TWeakObjectPtr<ASkyLight> SpawnedSky;
    TWeakObjectPtr<AExponentialHeightFog> Haze;
    TWeakObjectPtr<UStaticMeshComponent> SkyDome;
    const FRegimentSkyProfile* ActiveSkyProfile = nullptr;
    FRotator OriginalSunRotation = FRotator::ZeroRotator;
    EComponentMobility::Type OriginalSunMobility = EComponentMobility::Movable;
    bool bOriginalSkyRealTime = false;
    float OriginalSunIntensity = 0.f;
    float OriginalSunTemperature = 6500.f;
    float OriginalSunAngle = 0.f;
    float OriginalSkyIntensity = 0.f;
    bool bOriginalUseTemperature = false;

    void Initialize(UWorld& World, AActor& Owner, const FVector& Origin)
    {
        // Only the stock battlefield calls this. Reuse its active lights across different level revisions.
        for (TActorIterator<ADirectionalLight> It(&World); It; ++It)
        {
            ADirectionalLight* Sun = *It;
            auto* Light = Cast<UDirectionalLightComponent>(Sun->GetLightComponent());
            if (Sun->GetLevel() != World.PersistentLevel || !Light || Light->Mobility == EComponentMobility::Static ||
                !Light->IsVisible() || !Light->bAtmosphereSunLight || Sun->GetActorForwardVector().Z >= -.20)
                continue;
            StockSun = Light;
            OriginalSunIntensity = Light->Intensity;
            OriginalSunRotation = Light->GetComponentRotation();
            OriginalSunMobility = Light->Mobility;
            // A registered Stationary light keeps its authored angle in a play world.
            Light->SetMobility(EComponentMobility::Movable);
            OriginalSunTemperature = Light->Temperature;
            OriginalSunAngle = Light->LightSourceAngle;
            bOriginalUseTemperature = Light->bUseTemperature;
            break;
        }
        FActorSpawnParameters Spawn;
        Spawn.Owner = &Owner;
        Spawn.ObjectFlags |= RF_Transient;
        if (!StockSun.IsValid())
        {
            if (auto* Sun = World.SpawnActor<ADirectionalLight>(Origin, FRotator(-49, -27, 0), Spawn))
            {
                auto* Light = CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent());
                Light->SetMobility(EComponentMobility::Movable);
                Light->SetAtmosphereSunLight(true);
                StockSun = Light;
                OriginalSunRotation = Light->GetComponentRotation();
                SpawnedSun = Sun;
            }
        }
        for (TActorIterator<ASkyLight> It(&World); It; ++It)
        {
            ASkyLight* Sky = *It;
            USkyLightComponent* Light = Sky->GetLightComponent();
            if (Sky->GetLevel() != World.PersistentLevel || !Light || Light->Mobility == EComponentMobility::Static ||
                !Light->IsVisible())
                continue;
            StockSky = Light;
            OriginalSkyIntensity = Light->Intensity;
            bOriginalSkyRealTime = Light->bRealTimeCapture;
            break;
        }
        if (!StockSky.IsValid())
        {
            if (auto* Sky = World.SpawnActor<ASkyLight>(Origin + FVector(0, 0, 600), FRotator::ZeroRotator, Spawn))
            {
                auto* Light = Sky->GetLightComponent();
                Light->SetMobility(EComponentMobility::Movable);
                Light->RecaptureSky();
                StockSky = Light;
                SpawnedSky = Sky;
            }
        }
        CreateSkyDome(World, Owner, Origin);
        if (USkyLightComponent* Light = StockSky.Get(); SkyDome.IsValid() && Light && Light->bRealTimeCapture)
            Light->SetRealTimeCapture(false);
        if (TActorIterator<AExponentialHeightFog>(&World))
            return;
        Haze = World.SpawnActor<AExponentialHeightFog>(Origin - FVector(0, 0, 700), FRotator::ZeroRotator, Spawn);
    }

    void CreateSkyDome(UWorld& World, AActor& Owner, const FVector& Origin)
    {
        auto* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
        auto* Material = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Game/Battle/Materials/Terrain/M_RegimentSkyDome.M_RegimentSkyDome"));
        if (!Sphere || !Material)
            return;
        auto* Dome = NewObject<UStaticMeshComponent>(&Owner, NAME_None, RF_Transient);
        Owner.AddInstanceComponent(Dome);
        ConfigureLandscapePrimitive(*Dome);
        Dome->SetMobility(EComponentMobility::Movable);
        Dome->SetStaticMesh(Sphere);
        Dome->SetAbsolute(true, true, true);
        Dome->SetWorldTransform(FTransform(FQuat::Identity, Origin, FVector(20000.0)));
        Dome->SetMaterial(0, UMaterialInstanceDynamic::Create(Material, &Owner));
        Dome->RegisterComponentWithWorld(&World);
        SkyDome = Dome;
    }

    void UseProfile(battle::MarchTerrain Terrain, battle::MarchField Field)
    {
        const bool bCold = HasWinterSurroundings(Terrain);
        const bool bDry = HasDrySurroundings(Terrain);
        const auto& SkyProfile = SkyProfileFor(Terrain);
        const FRotator LightingAngle(SkyProfile.SunPitch, OriginalSunRotation.Yaw + SkyProfile.SunYawOffset, 0.f);
        if (UDirectionalLightComponent* Light = StockSun.Get())
        {
            Light->SetWorldRotation(LightingAngle);
            Light->SetTemperature(SkyProfile.Temperature);
            Light->SetUseTemperature(true);
            Light->SetIntensity(bCold ? 7.2f : bDry ? 8.5f : 8.f);
            Light->SetLightSourceAngle(bCold ? 1.8f : bDry ? 1.1f : 1.4f);
        }
        if (USkyLightComponent* Light = StockSky.Get())
            Light->SetIntensity(bCold ? 1.50f : bDry ? 1.24f : 1.38f);
        if (ActiveSkyProfile != &SkyProfile)
        {
            if (auto* Dome = SkyDome.Get())
            {
                auto* Material = CastChecked<UMaterialInstanceDynamic>(Dome->GetMaterial(0));
                const FVector Sun = -LightingAngle.Vector();
                Material->SetVectorParameterValue(TEXT("SunDirection"), {float(Sun.X), float(Sun.Y), float(Sun.Z), 1.f});
                Material->SetVectorParameterValue(TEXT("Zenith"), SkyProfile.Zenith);
                Material->SetVectorParameterValue(TEXT("Horizon"), SkyProfile.Horizon);
                Material->SetVectorParameterValue(TEXT("CloudShade"), SkyProfile.CloudShade);
                Material->SetVectorParameterValue(TEXT("CloudLight"), SkyProfile.CloudLight);
                Material->SetVectorParameterValue(TEXT("SunGlow"), SkyProfile.SunGlow);
                Material->SetVectorParameterValue(TEXT("CloudLayout"), SkyProfile.CloudLayout);
                if (auto* Light = StockSky.Get(); Light && Light->SourceType == SLS_CapturedScene)
                    Light->RecaptureSky();
            }
            ActiveSkyProfile = &SkyProfile;
        }
        if (AExponentialHeightFog* Fog = Haze.Get())
        {
            auto* Component = Fog->GetComponent();
            Component->SetVolumetricFog(false);
            Component->SetFogHeightFalloff(.15f);
            Component->SetFogInscatteringColor(bCold ? FLinearColor(.34f, .40f, .49f) :
                bDry ? FLinearColor(.42f, .34f, .22f) : FLinearColor(.29f, .36f, .40f));
            Component->SetDirectionalInscatteringColor(FLinearColor::Black);
            Component->SetStartDistance(float(FMath::Max(24000.0, Cm * FMath::Max(Field.HalfWidth(), Field.HalfHeight()) * 1.65)));
            Component->SetFogMaxOpacity(bCold ? .30f : bDry ? .20f : .24f);
            Component->SetFogDensity(bCold ? .008f : bDry ? .005f : .006f);
        }
    }

    ~FRegimentDaylight()
    {
        const bool bHadDome = SkyDome.IsValid();
        if (auto* Dome = SkyDome.Get())
            Dome->DestroyComponent();
        if (AExponentialHeightFog* Fog = Haze.Get())
            Fog->Destroy();
        if (auto* Sky = SpawnedSky.Get())
            Sky->Destroy();
        else if (USkyLightComponent* Light = StockSky.Get())
        {
            Light->SetIntensity(OriginalSkyIntensity);
            if (Light->bRealTimeCapture != bOriginalSkyRealTime)
                Light->SetRealTimeCapture(bOriginalSkyRealTime);
            else if (bHadDome && Light->SourceType == SLS_CapturedScene)
                Light->RecaptureSky();
        }
        if (auto* Sun = SpawnedSun.Get())
            Sun->Destroy();
        else if (UDirectionalLightComponent* Light = StockSun.Get())
        {
            Light->SetWorldRotation(OriginalSunRotation);
            Light->SetUseTemperature(bOriginalUseTemperature);
            Light->SetTemperature(OriginalSunTemperature);
            Light->SetIntensity(OriginalSunIntensity);
            Light->SetLightSourceAngle(OriginalSunAngle);
            Light->SetMobility(OriginalSunMobility);
        }
    }
};

bool IsGroundColor(EBoardColor Color)
{
    using B = EBoardColor;
    switch (Color)
    {
    case B::Meadow: case B::Lawn: case B::DryGrass: case B::Bank:
    case B::Dust: case B::Rut: case B::Harvest: case B::Stubble: case B::Plain:
    case B::Snow: case B::ShelteredSnow: case B::DistantGrass: case B::DistantField:
    case B::Ditch: case B::EarthBank: case B::BankTurf:
        return true;
    default:
        return false;
    }
}

bool IsRiverColor(EBoardColor Color)
{
    return Color == EBoardColor::DeepWater || Color == EBoardColor::Ford;
}

FLinearColor LinearColor(uint32 SRGB)
{
    return FLinearColor::FromSRGBColor(FColor(uint8(SRGB >> 16), uint8(SRGB >> 8), uint8(SRGB), 255));
}

uint32 TerrainColor(EBoardColor Color, battle::MarchTerrain Terrain)
{
    using T = battle::MarchTerrain;
    using B = EBoardColor;
    if (HasWinterSurroundings(Terrain))
    {
        switch (Color)
        {
        case B::Meadow:
        case B::Snow: return 0xC5D2D5;
        case B::Lawn:
        case B::ShelteredSnow: return 0xA9BBB9;
        case B::DistantGrass: return 0xB2BCBD;
        case B::DistantField: return 0x97A8AC;
        case B::BankTurf: return 0xA7B1A6;
        case B::EarthBank: return 0x706353;
        case B::Ditch: return 0x645A50;
        case B::DeepWater: return 0x3B667C;
        default: break;
        }
    }
    else if (Terrain == T::Waterloo)
    {
        switch (Color)
        {
        case B::Meadow: return 0x869779;
        case B::Lawn: return 0x76896B;
        case B::DryGrass: return 0xA19F70;
        case B::Harvest: return 0xA6A18A;
        case B::Bank: return 0x8B866A;
        case B::DistantGrass: return 0x80978B;
        case B::DistantField: return 0xA3A18A;
        case B::PineShade: return 0x495F4D;
        case B::Pine: return 0x576E55;
        case B::PineLight: return 0x6B8063;
        default: break;
        }
    }
    else if (HasDrySurroundings(Terrain))
    {
        switch (Color)
        {
        case B::Meadow: return 0xAA9D62;
        case B::Lawn: return 0x96915A;
        case B::DryGrass: return 0xC0AC70;
        case B::Harvest: return 0xC3AC73;
        case B::Stubble: return 0xCDB981;
        case B::DistantGrass: return 0xA09771;
        case B::DistantField: return 0xBAA786;
        case B::PineShade: return 0x5A6440;
        case B::Pine: return 0x66734D;
        case B::PineLight: return 0x798559;
        default: break;
        }
    }
    return BoardColors[int32(Color)];
}

FLinearColor EarthTone(EBoardColor Color, battle::MarchTerrain Terrain, const FLinearColor& Base)
{
    using B = EBoardColor;
    if (Color == B::Meadow || Color == B::Lawn || Color == B::Harvest || Color == B::Plain ||
        Color == B::DistantGrass || Color == B::Snow)
    {
        if (HasWinterSurroundings(Terrain))
            return LinearColor(0xA2ABAA);
        if (HasDrySurroundings(Terrain))
            return LinearColor(0x987E4C);
        if (Terrain == battle::MarchTerrain::Waterloo)
            return LinearColor(0x8A8462);
        return LinearColor(0x9A9160);
    }
    return FMath::Lerp(Base, LinearColor(0x766244), .24f);
}

TArray<FShapeBatch> MakeTerrain(battle::MarchTerrain Terrain, battle::MarchField Field)
{
    if (Terrain == battle::MarchTerrain::Cannae || Terrain == battle::MarchTerrain::Peipus)
        return MakeHistoricTerrain(Terrain, Field);
    if (Terrain == battle::MarchTerrain::River || Terrain == battle::MarchTerrain::StirlingBridge)
        return MakeRiverTerrain(Terrain, Field);

    if (battle::MarchUsesDetailedTerrain(Terrain))
        return MakeDetailedTerrain(Terrain, Field);

    TArray<FShapeBatch> Batches;
    const double BoardHalfWidth = Cm * Field.HalfWidth();
    const double BoardHalfDepth = Cm * Field.HalfHeight();
    const double Width = 2.0 * BoardHalfWidth;
    const double Depth = 2.0 * BoardHalfDepth;
    const bool bWalls = Terrain == battle::MarchTerrain::StoneWalls;
    AddBox(Batches, EBoardColor::Earth, {0.0, 0.0, -260.0}, {Width, Depth, 360.0});
    AddBox(Batches, EBoardColor::Rim, {0.0, 0.0, -378.0}, {Width + 95.0, Depth + 95.0, 50.0});
    if (const auto Hill = battle::MarchHill(Terrain, Field))
    {
        AddWoodland(Batches, Field, &*Hill);
        return Batches;
    }
    if (bWalls)
        AddBox(Batches, EBoardColor::Meadow, {0.0, 0.0, -40.0}, {Width, Depth, 80.0});

    if (bWalls)
    {
        for (const auto& Wall : battle::MarchObstacles(Terrain, Field))
            if (Wall.feature == battle::TerrainFeature::Hedge)
                AddHedge(Batches, Wall);
    }
    else
    {
        // Harvest strips are kept to the sides of the three approach lanes.
        for (double Side : {-1.0, 1.0})
            for (int32 Strip = 0; Strip < 5; ++Strip)
            {
                const double Y = Side * (900.0 + Strip * 159.0) * BoardHalfDepth / (Cm * battle::MarchHalfHeight);
                AddBox(Batches, Strip % 2 == 0 ? EBoardColor::Stubble : EBoardColor::DryGrass,
                    {Side * 500.0, Y, .6}, {Width - 2000.0, 98.0, 2.0}, Side * 1.0);
            }
    }
    AddWoodland(Batches, Field);
    return Batches;
}

TArray<FShapeBatch> MakeToken(int32 Team)
{
    const EBoardColor TeamColor = Team == 0 ? EBoardColor::Blue : EBoardColor::Orange;
    const EBoardColor LightColor = Team == 0 ? EBoardColor::BlueLight : EBoardColor::OrangeLight;
    TArray<FShapeBatch> Batches;
    AddBox(Batches, EBoardColor::Earth, { 0.0, 0.0, 64.0 },
        { FRegimentMarchBoard::TokenLengthCm, FRegimentMarchBoard::TokenWidthCm, 62.0 });
    AddBox(Batches, TeamColor, { 0.0, 0.0, 97.0 }, { 545.0, 375.0, 58.0 });
    AddBox(Batches, LightColor, { 245.0, 0.0, 131.0 }, { 38.0, 325.0, 13.0 });

    for (double X : { -116.0, 0.0, 116.0 })
    {
        for (double Y : { -82.0, 82.0 })
        {
            AddShape(Batches, EShape::Cylinder, LightColor, { X, Y, 173.0 }, { 56.0, 56.0, 93.0 });
            AddShape(Batches, EShape::Sphere, EBoardColor::Ivory, { X + 7.0, Y, 234.0 }, { 42.0, 42.0, 43.0 });
        }
    }

    AddShape(Batches, EShape::Cylinder, EBoardColor::Ivory, { -205.0, -117.0, 251.0 }, { 23.0, 23.0, 258.0 });
    AddShape(Batches, EShape::Box, LightColor, { -205.0, -49.0, 358.0 }, { 91.0, 150.0, 18.0 },
        FRotator(0.0, 0.0, -26.0));
    return Batches;
}

TArray<FShapeBatch> MakeSelection()
{
    TArray<FShapeBatch> Batches;
    AddBox(Batches, EBoardColor::Brass, { 0.0, 0.0, 49.0 }, { 652.0, 482.0, 24.0 });
    return Batches;
}

TArray<FShapeBatch> MakeWaypoint()
{
    TArray<FShapeBatch> Batches;
    AddBox(Batches, EBoardColor::Timber, { 0.0, 0.0, 55.0 }, { 185.0, 185.0, 50.0 }, 45.0);
    AddBox(Batches, EBoardColor::Brass, { 0.0, 0.0, 82.0 }, { 143.0, 143.0, 28.0 }, 45.0);
    AddShape(Batches, EShape::Cylinder, EBoardColor::Ivory, { 0.0, 0.0, 212.0 }, { 26.0, 26.0, 263.0 });
    AddShape(Batches, EShape::Box, EBoardColor::Brass, { 76.0, 0.0, 322.0 }, { 162.0, 100.0, 16.0 },
        FRotator(-23.0, 0.0, 0.0));
    AddShape(Batches, EShape::Box, EBoardColor::Ivory, { 142.0, 0.0, 353.0 }, { 28.0, 100.0, 15.0 },
        FRotator(-23.0, 0.0, 0.0));
    return Batches;
}
}

void ApplyRegimentBattleCameraExposure(UCameraComponent& Camera)
{
    const UWorld* World = Camera.GetWorld();
    if (World && IsStockBattleMap(*World))
    {
        auto& Settings = Camera.PostProcessSettings;
        Settings.bOverride_AutoExposureMethod = true;
        Settings.AutoExposureMethod = AEM_Manual;
        Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
        Settings.AutoExposureApplyPhysicalCameraExposure = false;
        Settings.bOverride_AutoExposureBias = true;
        Settings.AutoExposureBias = -.25f;
        Settings.bOverride_LocalExposureShadowContrastScale = true;
        Settings.LocalExposureShadowContrastScale = .72f;
        Settings.bOverride_BloomIntensity = true;
        Settings.BloomIntensity = .10f;
        Camera.PostProcessBlendWeight = 1.f;
    }
}

struct FRegimentMarchBoard::FScene
{
    FScene(AActor& InOwner, const FVector& Origin)
        : Owner(&InOwner), BoardOrigin(Origin)
    {
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(ShapePaths); ++Index)
            Shapes[Index].Reset(LoadObject<UStaticMesh>(nullptr, ShapePaths[Index]));

        UMaterialInterface* Basic = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
        UMaterialInterface* Ground = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Game/Battle/Materials/Terrain/M_RegimentTerrain.M_RegimentTerrain"));
        UMaterialInterface* Water = LoadObject<UMaterialInterface>(nullptr,
            TEXT("/Game/Battle/Materials/Terrain/M_RegimentRiver.M_RegimentRiver"));
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(BoardColors); ++Index)
        {
            const auto Color = EBoardColor(Index);
            UMaterialInterface* Base = IsGroundColor(Color) && Ground ? Ground
                : IsRiverColor(Color) && Water ? Water : Basic;
            if (Base)
                Colors[Index].Reset(UMaterialInstanceDynamic::Create(Base, &InOwner));
        }

        if (UWorld* World = InOwner.GetWorld())
        {
            bStockMap = IsStockBattleMap(*World);
            if (bStockMap)
            {
                Daylight.Initialize(*World, InOwner, BoardOrigin);
                if (Basic && !FParse::Param(FCommandLine::Get(), TEXT("RegimentNoGrass")))
                    GrassMesh.Reset(MakeRegimentGrassMesh(InOwner, *Basic));
            }

            const FRotator View(-80.0, -90.0, 0.0);
            FActorSpawnParameters Spawn;
            Spawn.Owner = &InOwner;
            Spawn.ObjectFlags |= RF_Transient;
            Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

            if (ACameraActor* Camera = World->SpawnActor<ACameraActor>(Origin - View.Vector() * DefaultViewDistance, View, Spawn))
            {
                UCameraComponent* Lens = Camera->GetCameraComponent();
                Lens->SetFieldOfView(45.0f);
                Lens->SetAspectRatio(16.0f / 9.0f);
                Lens->SetConstraintAspectRatio(true);
                Lens->PostProcessSettings.bOverride_AutoExposureBias = true;
                Lens->PostProcessSettings.AutoExposureBias = -1.65f;
                Lens->PostProcessSettings.bOverride_BloomIntensity = true;
                Lens->PostProcessSettings.BloomIntensity = .10f;
                Lens->PostProcessBlendWeight = 1.f;
                ApplyRegimentBattleCameraExposure(*Lens);
                OverviewCamera = Camera;
            }
        }
        SetTerrain(battle::MarchTerrain::River, {});
    }

    ~FScene()
    {
        ClearMarkers();
        RoutePreview.Destroy();
        Exterior.Destroy();
        Terrain.Destroy();
        if (ACameraActor* Camera = OverviewCamera.Get())
            Camera->Destroy();
    }

    FBoardVisual CreateVisual(USceneComponent* Parent, const TArray<FShapeBatch>& Batches,
        bool bBackground = false) const
    {
        FBoardVisual Visual;
        AActor* Actor = Owner.Get();
        if (!Actor)
            return Visual;

        USceneComponent* Root = NewObject<USceneComponent>(Actor, NAME_None, RF_Transient);
        Root->SetMobility(EComponentMobility::Movable);
        Actor->AddInstanceComponent(Root);
        if (Parent)
            Root->SetupAttachment(Parent);
        Root->RegisterComponent();
        Visual.Root = Root;

        for (const FShapeBatch& Batch : Batches)
        {
            UStaticMesh* Mesh = Shapes[int32(Batch.Shape)].Get();
            if (!Mesh)
                continue;

            auto* Component = NewObject<UInstancedStaticMeshComponent>(Actor, NAME_None, RF_Transient);
            Component->SetMobility(EComponentMobility::Movable);
            Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            Component->SetCanEverAffectNavigation(false);
            if (bBackground)
            {
                ConfigureLandscapePrimitive(*Component);
                Component->SetCullDistances(0, 85000);
            }
            Component->SetStaticMesh(Mesh);
            if (UMaterialInterface* Material = Colors[int32(Batch.Color)].Get())
                Component->SetMaterial(0, Material);
            Actor->AddInstanceComponent(Component);
            Component->SetupAttachment(Root);
            Component->AddInstances(Batch.Instances, false, false, false);
            Component->RegisterComponent();
            Visual.Meshes.Add(Component);
        }
        return Visual;
    }

    void SetExterior(USceneComponent& Parent, FSurroundingLandscape Landscape)
    {
        for (auto& Batch : Landscape.Landmarks)
        {
            auto* Existing = Landscape.Groves.FindByPredicate([&Batch](const FShapeBatch& Other)
            {
                return Other.Shape == Batch.Shape && Other.Color == Batch.Color;
            });
            if (Existing)
                Existing->Instances.Append(MoveTemp(Batch.Instances));
            else
                Landscape.Groves.Add(MoveTemp(Batch));
        }
        Exterior = CreateVisual(&Parent, Landscape.Groves, true);
        if (USceneComponent* Root = Exterior.Root.Get())
            AddSurface(*Root, Exterior, MoveTemp(Landscape.Ground), Landscape.Materials, true);
    }

    void UpdateTerrainPalette(battle::MarchTerrain Choice, const battle::MarchField& Field) const
    {
        const FLinearColor Origin(float(BoardOrigin.X), float(BoardOrigin.Y), float(BoardOrigin.Z), 1.f);
        const bool bLongBattlefield = Field.HalfWidth() > 140.f;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(BoardColors); ++Index)
        {
            auto* Material = Colors[Index].Get();
            if (!Material)
                continue;

            const auto Color = EBoardColor(Index);
            const FLinearColor BaseColor = LinearColor(TerrainColor(Color, Choice));
            Material->SetVectorParameterValue(TEXT("Color"), BaseColor);
            Material->SetScalarParameterValue(TEXT("Roughness"), .89f);
            if (IsGroundColor(Color))
            {
                Material->SetVectorParameterValue(TEXT("BoardOrigin"), Origin);
                Material->SetVectorParameterValue(TEXT("EarthTone"), EarthTone(Color, Choice, BaseColor));
                Material->SetScalarParameterValue(TEXT("PatternScale"), bLongBattlefield ? .00022f : .00070f);
            }
            else if (IsRiverColor(Color))
            {
                const bool bFord = Color == EBoardColor::Ford;
                const bool bAcross = Choice == battle::MarchTerrain::Cannae ||
                    Choice == battle::MarchTerrain::Borodino || Choice == battle::MarchTerrain::Narva;
                Material->SetVectorParameterValue(TEXT("BoardOrigin"), Origin);
                Material->SetVectorParameterValue(TEXT("FlowDirection"),
                    bAcross ? FLinearColor(1, 0, 0) : FLinearColor(0, 1, 0));
                Material->SetVectorParameterValue(TEXT("Sheen"), LinearColor(bFord ? 0xB4C7A9 : 0x709DAD));
                Material->SetScalarParameterValue(TEXT("Speed"), bFord ? 39.f : 66.f);
            }
        }
    }

    void AddSurface(USceneComponent& Parent, FBoardVisual& Visual,
        UE::Geometry::FDynamicMesh3 Mesh, TConstArrayView<EBoardColor> Materials, bool bBackground = false) const
    {
        AActor* Actor = Owner.Get();
        if (!Actor || Mesh.TriangleCount() == 0)
            return;
        auto* Component = NewObject<UDynamicMeshComponent>(Actor, NAME_None, RF_Transient);
        Component->SetMobility(EComponentMobility::Movable);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetCanEverAffectNavigation(false);
        if (bBackground)
            ConfigureLandscapePrimitive(*Component);
        for (int32 Index = 0; Index < Materials.Num(); ++Index)
            Component->SetMaterial(Index, Colors[int32(Materials[Index])].Get());
        Component->SetMesh(MoveTemp(Mesh));
        Actor->AddInstanceComponent(Component);
        Component->SetupAttachment(&Parent);
        Component->RegisterComponent();
        Visual.Surfaces.Add(Component);
    }

    void AddGrass(USceneComponent& Parent, battle::MarchTerrain Choice, const battle::MarchField& Field,
        const battle::TerrainHill* Hill, std::span<const battle::Obstacle> Areas)
    {
        AActor* Actor = Owner.Get();
        UStaticMesh* Mesh = GrassMesh.Get();
        if (!Actor || !Mesh)
            return;
        const TArray<FTransform> Instances = BuildRegimentGrassTransforms(Choice, Field, Hill, Areas);
        if (Instances.IsEmpty())
            return;

        auto* Component = NewObject<UHierarchicalInstancedStaticMeshComponent>(Actor, NAME_None, RF_Transient);
        Component->SetMobility(EComponentMobility::Movable);
        ConfigureLandscapePrimitive(*Component);
        Component->SetGenerateOverlapEvents(false);
        Component->SetCullDistances(0, RegimentGrassCullDistanceCm);
        Component->SetStaticMesh(Mesh);
        if (auto* Material = UMaterialInstanceDynamic::Create(Mesh->GetMaterial(0), Component))
        {
            const bool bDry = HasDrySurroundings(Choice);
            const EBoardColor Floor = Choice == battle::MarchTerrain::Cannae ? EBoardColor::Plain : EBoardColor::Meadow;
            const FLinearColor Ground = LinearColor(TerrainColor(Floor, Choice));
            Material->SetVectorParameterValue(TEXT("Color"),
                FMath::Lerp(Ground, LinearColor(bDry ? 0x827342 : 0x526641), .63f));
            Material->SetScalarParameterValue(TEXT("Roughness"), .94f);
            Component->SetMaterial(0, Material);
        }
        Actor->AddInstanceComponent(Component);
        Component->SetupAttachment(&Parent);
        Component->bAutoRebuildTreeOnInstanceChanges = false;
        Component->AddInstances(Instances, false, false, false);
        Component->BuildTreeIfOutdated(false, false);
        Component->RegisterComponent();
        Terrain.Meshes.Add(Component);
    }

    void AddMarkerLabel(FBoardVisual& Visual, uint64 Id, float Height, float OffsetY, FColor Color) const
    {
        AActor* Actor = Owner.Get();
        USceneComponent* Root = Visual.Root.Get();
        if (!Actor || !Root)
            return;

        auto* Label = NewObject<UTextRenderComponent>(Actor, NAME_None, RF_Transient);
        Label->SetMobility(EComponentMobility::Movable);
        Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Label->SetCanEverAffectNavigation(false);
        Label->SetCastShadow(false);
        Label->SetText(FText::AsNumber(MissionTeamSlot(int32(Id - 1)) + 1));
        Label->SetHorizontalAlignment(EHTA_Center);
        Label->SetVerticalAlignment(EVRTA_TextCenter);
        Label->SetWorldSize(132.f);
        Label->SetTextRenderColor(Color);
        Actor->AddInstanceComponent(Label);
        Label->SetupAttachment(Root);
        // Keep the numeral readable from overhead while the regiment turns.
        Label->SetAbsolute(false, true, false);
        Label->SetRelativeLocation(FVector(0, OffsetY, Height));
        Label->SetRelativeRotation(FRotator(78, -90, 0));
        Label->RegisterComponent();
        Visual.Label = Label;
    }

    void SetTerrain(battle::MarchTerrain Choice, const battle::MarchField& Field)
    {
        AActor* Actor = Owner.Get();
        if (!Actor || (Choice == ActiveTerrain && Field == ActiveField && Terrain.Root.IsValid()))
            return;

        ClearMarkers();
        RoutePreview.Destroy();
        Exterior.Destroy();
        Terrain.Destroy();
        ActiveTerrain = Choice;
        ActiveField = Field;
        UpdateTerrainPalette(Choice, Field);
        Terrain = CreateVisual(Actor->GetRootComponent(), MakeTerrain(Choice, Field));
        const auto Hill = battle::MarchHill(Choice, Field);
        const FVector2D HalfExtent(Cm * Field.HalfWidth(), Cm * Field.HalfHeight());
        if (bStockMap)
            Daylight.UseProfile(Choice, Field);
        if (auto* Camera = OverviewCamera.Get())
        {
            const double Distance = DefaultViewDistance * MissionCameraScale(Field);
            const double FocusHeight = Hill ? Hill->height_m * 25.0 : 0.0;
            Camera->SetActorLocation(BoardOrigin + FVector(0, 0, FocusHeight) - Camera->GetActorForwardVector() * Distance);
        }
        if (USceneComponent* Root = Terrain.Root.Get())
        {
            Root->SetAbsolute(true, true, true);
            Root->SetWorldLocationAndRotation(BoardOrigin, FRotator::ZeroRotator);
            Root->SetWorldScale3D(FVector::OneVector);
            using B = EBoardColor;
            using T = battle::MarchTerrain;
            auto Areas = battle::MarchTerrainAreas(Choice, Field);
            if (Hill || battle::MarchUsesDetailedTerrain(Choice) || Choice == T::OpenField || Choice == T::Cannae)
            {
                auto Mesh = BuildRegimentGroundMesh(Hill ? &*Hill : nullptr, HalfExtent.X, HalfExtent.Y, Areas);
                if (bStockMap)
                    SetExterior(*Root, MakeSurroundingLandscape(Choice, HalfExtent, &Mesh, Areas));
                const B GroundMaterials[] = {Choice == T::Cannae ? B::Plain : B::Meadow,
                    B::Stubble, B::Dust, B::Harvest, B::Lawn, B::DeepWater, B::Ford, B::Bank,
                    B::Ditch, B::EarthBank, B::BankTurf};
                static_assert(UE_ARRAY_COUNT(GroundMaterials) == int32(ERegimentGroundMaterial::BankTurf) + 1);
                AddSurface(*Root, Terrain, MoveTemp(Mesh), GroundMaterials);
            }
            else if (bStockMap)
                SetExterior(*Root, MakeSurroundingLandscape(Choice, HalfExtent, nullptr, Areas));

            if (Choice == T::StoneWalls)
            {
                constexpr B WallMaterials[] = {B::WallMortar, B::WallStone, B::WallPale, B::WallCap};
                const auto Walls = battle::MarchObstacles(Choice, Field);
                AddSurface(*Root, Terrain, BuildRegimentWallMesh(Walls), WallMaterials);
                for (const auto& Crossing : battle::MarchCrossings)
                    Areas.push_back({-19.5f, Crossing.y - 2.2f, 19.5f, Crossing.y + 2.2f, false, battle::TerrainFeature::Road});
            }
            constexpr B RoadMaterials[] = {B::Dust, B::Rut};
            AddSurface(*Root, Terrain,
                BuildRegimentRoadMesh(Hill ? &*Hill : nullptr, HalfExtent.X, HalfExtent.Y, Areas), RoadMaterials, true);
            AddGrass(*Root, Choice, Field, Hill ? &*Hill : nullptr, Areas);
        }
    }

    void Update(TConstArrayView<FMarchBoardMarker> Markers, uint64 Selected)
    {
        USceneComponent* Root = Terrain.Root.Get();
        if (!Root || !Owner.IsValid())
            return;

        TSet<uint64> VisibleIds;
        for (const FMarchBoardMarker& Marker : Markers)
        {
            if (!Marker.bVisible)
                continue;

            VisibleIds.Add(Marker.Id);
            FRegimentVisual& Visual = Regiments.FindOrAdd(Marker.Id);
            if (!Visual.Token.Root.IsValid() || Visual.Team != Marker.Team)
            {
                Visual.Destroy();
                Visual.Team = Marker.Team;
                Visual.Token = CreateVisual(Root, MakeToken(Marker.Team));
                AddMarkerLabel(Visual.Token, Marker.Id, 410.f, 40.f, FColor(255, 248, 218));
                Visual.Selection = CreateVisual(Visual.Token.Root.Get(), MakeSelection());
            }

            if (USceneComponent* Token = Visual.Token.Root.Get())
            {
                Token->SetWorldLocationAndRotation(Marker.Position, FRotator(0.0, Marker.YawDegrees, 0.0));
                Token->SetWorldScale3D(FVector(Marker.SizeScale.X, Marker.SizeScale.Y, 1.0));
            }
            if (USceneComponent* Selection = Visual.Selection.Root.Get())
                Selection->SetVisibility(Marker.Id == Selected, true);

            if (Marker.Waypoint.IsSet())
            {
                if (!Visual.Waypoint.Root.IsValid())
                {
                    Visual.Waypoint = CreateVisual(Root, MakeWaypoint());
                    AddMarkerLabel(Visual.Waypoint, Marker.Id, 400.f, 0.f, FColor(255, 220, 136));
                }
                if (USceneComponent* Waypoint = Visual.Waypoint.Root.Get())
                    Waypoint->SetWorldLocation(Marker.Waypoint.GetValue());
            }
            else if (Visual.Waypoint.Root.IsValid())
            {
                Visual.Waypoint.Destroy();
            }
        }

        for (auto It = Regiments.CreateIterator(); It; ++It)
        {
            if (!VisibleIds.Contains(It.Key()))
            {
                It.Value().Destroy();
                It.RemoveCurrent();
            }
        }
    }

    void SetRoutePreview(TConstArrayView<FMarchBoardRouteSegment> Segments)
    {
        USceneComponent* Root = Terrain.Root.Get();
        if (!Root || (RouteSegments.Num() == Segments.Num() &&
            (Segments.IsEmpty() || std::equal(RouteSegments.GetData(),
                RouteSegments.GetData() + RouteSegments.Num(), Segments.GetData()))))
            return;

        if (!RoutePreview.Root.IsValid())
        {
            const TArray<FShapeBatch> Batches{
                {EShape::Box, EBoardColor::RouteOpen, {}},
                {EShape::Box, EBoardColor::RouteBlocked, {}}
            };
            RoutePreview = CreateVisual(Root, Batches);
        }

        const auto Hill = battle::MarchHill(ActiveTerrain, ActiveField);
        const auto SurfacePoint = [&](const FVector& World)
        {
            const FVector Local = World - BoardOrigin;
            const auto Meters = battle::Vec3{float(Local.X / Cm), float(Local.Y / Cm), 0.f};
            return FVector(Local.X, Local.Y, 66.0 + (Hill ? Cm * battle::HillHeight(*Hill, Meters) : 0.0));
        };
        TArray<FTransform> Instances[2];
        for (const auto& Segment : Segments)
        {
            const double Distance = FVector::Dist2D(Segment.From, Segment.To);
            if (Distance < 1.0)
                continue;
            const int32 Count = FMath::CeilToInt(Distance / (Segment.bReachable ? 165.0 : 85.0));
            auto& Batch = Instances[Segment.bReachable ? 0 : 1];
            const int32 PartStep = Segment.bReachable ? 1 : 2;
            for (int32 Part = 0; Part < Count; Part += PartStep)
            {
                const FVector From = SurfacePoint(FMath::Lerp(Segment.From, Segment.To, double(Part) / Count));
                const FVector To = SurfacePoint(FMath::Lerp(Segment.From, Segment.To, double(Part + 1) / Count));
                const FVector Direction = To - From;
                Batch.Emplace(Direction.Rotation(), (From + To) / 2.0,
                    FVector((Direction.Length() + 2.0) / Cm, .34, .055));
            }
        }
        for (int32 Batch = 0; Batch < RoutePreview.Meshes.Num(); ++Batch)
            if (auto* Mesh = RoutePreview.Meshes[Batch].Get())
            {
                Mesh->SetCastShadow(false);
                Mesh->ClearInstances();
                Mesh->AddInstances(Instances[Batch], false, false, false);
            }
        RouteSegments.Reset(Segments.Num());
        RouteSegments.Append(Segments.GetData(), Segments.Num());
    }

    void ClearMarkers()
    {
        SetRoutePreview({});
        for (auto& Pair : Regiments)
            Pair.Value.Destroy();
        Regiments.Reset();
    }

    TWeakObjectPtr<AActor> Owner;
    const FVector BoardOrigin;
    TWeakObjectPtr<ACameraActor> OverviewCamera;
    TStrongObjectPtr<UStaticMesh> GrassMesh;
    TStrongObjectPtr<UStaticMesh> Shapes[int32(EShape::Count)];
    TStrongObjectPtr<UMaterialInstanceDynamic> Colors[int32(EBoardColor::Count)];
    FBoardVisual Terrain;
    FBoardVisual Exterior;
    FRegimentDaylight Daylight;
    bool bStockMap = false;
    FBoardVisual RoutePreview;
    TArray<FMarchBoardRouteSegment> RouteSegments;
    battle::MarchTerrain ActiveTerrain = battle::MarchTerrain::River;
    battle::MarchField ActiveField;
    TMap<uint64, FRegimentVisual> Regiments;
};

FRegimentMarchBoard::FRegimentMarchBoard(AActor& Owner, const FVector& Origin)
    : Scene(MakeUnique<FScene>(Owner, Origin))
{
}

FRegimentMarchBoard::~FRegimentMarchBoard() = default;

void FRegimentMarchBoard::SetTerrain(battle::MarchTerrain Terrain, const battle::MarchField& Field)
{
    Scene->SetTerrain(Terrain, Field);
}

void FRegimentMarchBoard::Update(TConstArrayView<FMarchBoardMarker> Markers, uint64 Selected)
{
    Scene->Update(Markers, Selected);
}

void FRegimentMarchBoard::SetRoutePreview(TConstArrayView<FMarchBoardRouteSegment> Segments)
{
    Scene->SetRoutePreview(Segments);
}

void FRegimentMarchBoard::ClearMarkers()
{
    Scene->ClearMarkers();
}

ACameraActor* FRegimentMarchBoard::GetOverviewCamera() const
{
    return Scene->OverviewCamera.Get();
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentBattlefieldDaylightTest, "RegimentBattle.Presentation.BattlefieldDaylight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRegimentBattlefieldDaylightTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    AActor* Owner = World->SpawnActor<AActor>();
    auto* AuthoredSun = World->SpawnActor<ADirectionalLight>();
    auto* Light = CastChecked<UDirectionalLightComponent>(AuthoredSun->GetLightComponent());
    Light->SetMobility(EComponentMobility::Movable);
    Light->SetAtmosphereSunLight(true);
    Light->SetIntensity(2.f);
    AuthoredSun->SetActorRotation(FRotator(-35, 150, 0));
    Light->SetMobility(EComponentMobility::Stationary);
    {
        FRegimentDaylight Daylight;
        Daylight.Initialize(*World, *Owner, FVector::ZeroVector);
        Daylight.UseProfile(battle::MarchTerrain::Waterloo, {790, 535});
        TestEqual(TEXT("A live daylight source is reused even when its rotation or intensity changes"),
            Daylight.StockSun.Get(), Light);
        TestEqual(TEXT("The sky can rotate the existing stationary sunlight during play"),
            Light->Mobility.GetValue(), EComponentMobility::Movable);
        TestTrue(TEXT("Waterloo has direct daylight"), Light->Intensity >= 5.f);
        TestNotNull(TEXT("The field gets ambient fill when the level has no skylight"), Daylight.StockSky.Get());
        const float Temperate = Light->Temperature;
        const FRotator WaterlooSun = Light->GetComponentRotation();
        auto* Dome = Daylight.SkyDome.Get();
        TestNotNull(TEXT("The packaged sky material creates a dome"), Dome);
        const auto* SkyMaterial = Dome ? Cast<UMaterialInstanceDynamic>(Dome->GetMaterial(0)) : nullptr;
        FLinearColor AmberHorizon = FLinearColor::Black;
        if (SkyMaterial)
            TestTrue(TEXT("The shared material exposes the horizon color"),
                SkyMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Horizon")), AmberHorizon));
        Daylight.UseProfile(battle::MarchTerrain::Narva, {700, 480});
        TestTrue(TEXT("Winter light stays cooler and keeps a direct sun"), Light->Temperature > Temperate &&
            Light->Intensity >= 4.5f);
        TestTrue(TEXT("The winter and amber skies have different sun elevations"),
            Light->GetComponentRotation().Pitch < WaterlooSun.Pitch - 8.0);
        TestEqual(TEXT("Changing skies reuses the original dome"), Daylight.SkyDome.Get(), Dome);
        if (SkyMaterial)
        {
            FLinearColor WinterHorizon;
            TestTrue(TEXT("The winter sky has a bluer horizon than the amber sky"),
                SkyMaterial->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Horizon")), WinterHorizon) &&
                WinterHorizon.B > AmberHorizon.B && WinterHorizon.R < AmberHorizon.R);
        }
    }
    TestEqual(TEXT("Leaving the battlefield restores the authored sunlight"), Light->Intensity, 2.f);
    TestTrue(TEXT("Leaving the battlefield restores the original sun angle"),
        Light->GetComponentRotation().Equals(FRotator(-35, 150, 0), .01));
    TestEqual(TEXT("Leaving the battlefield restores stationary mobility"),
        Light->Mobility.GetValue(), EComponentMobility::Stationary);
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentBattlefieldSurroundingsTest, "RegimentBattle.Presentation.BattlefieldSurroundings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentBattlefieldSurroundingsTest::RunTest(const FString&)
{
    using T = battle::MarchTerrain;
    const std::pair<T, battle::MarchField> Cases[] = {
        {T::River, {80, 60}}, {T::Cannae, {}}, {T::Peipus, {300, 220}}, {T::Waterloo, {600, 400}},
        {T::OpenField, {160, 95}}, {T::Narva, {420, 300}}
    };
    for (const auto& [Terrain, Field] : Cases)
    {
        const FVector2D HalfExtent(Cm * Field.HalfWidth(), Cm * Field.HalfHeight());
        const auto Hill = battle::MarchHill(Terrain, Field);
        const auto Areas = battle::MarchTerrainAreas(Terrain, Field);
        const auto Core = Hill ? BuildRegimentHillMesh(*Hill, HalfExtent.X, HalfExtent.Y, Areas)
            : UE::Geometry::FDynamicMesh3{};
        const auto Surroundings = MakeSurroundingLandscape(Terrain, HalfExtent, Hill ? &Core : nullptr, Areas);
        if (!TestTrue(TEXT("The surroundings contain a bounded exterior mesh"),
            Surroundings.Ground.TriangleCount() > 0 && Surroundings.Ground.TriangleCount() <= 10000))
            return false;
        const auto Bounds = Surroundings.Ground.GetBounds();
        TestTrue(TEXT("Visible countryside extends beyond each side"),
            Bounds.Min.X < -HalfExtent.X - 100000.0 && Bounds.Max.X > HalfExtent.X + 100000.0 &&
            Bounds.Min.Y < -HalfExtent.Y - 100000.0 && Bounds.Max.Y > HalfExtent.Y + 100000.0);
        for (const int32 Id : Surroundings.Ground.VertexIndicesItr())
        {
            const auto Vertex = Surroundings.Ground.GetVertex(Id);
            if (FMath::Abs(Vertex.X) < HalfExtent.X && FMath::Abs(Vertex.Y) < HalfExtent.Y)
                return TestTrue(TEXT("Exterior mesh leaves playable ground alone"), false);
        }
        if (Terrain == T::Narva)
        {
            const int32 Water = Surroundings.Materials.IndexOfByKey(EBoardColor::DeepWater);
            if (!TestTrue(TEXT("Narva's river continues beyond the bank"), Water != INDEX_NONE))
                return false;
            const auto& Mesh = Surroundings.Ground;
            for (const int32 Id : Mesh.TriangleIndicesItr())
            {
                if (Mesh.Attributes()->GetMaterialID()->GetValue(Id) != Water)
                    continue;
                const auto Face = Mesh.GetTriangle(Id);
                for (const int32 Corner : {Face.A, Face.B, Face.C})
                    if (!FMath::IsNearlyZero(Mesh.GetVertex(Corner).Z))
                        return TestTrue(TEXT("Narva's exterior river stays level with its playable water"), false);
            }
        }
        int32 Instances = 0;
        for (const auto& Batch : Surroundings.Groves)
        {
            Instances += Batch.Instances.Num();
            for (const auto& Transform : Batch.Instances)
            {
                const auto Position = Transform.GetLocation();
                if (FMath::Abs(Position.X) < HalfExtent.X + 1000.0 &&
                    FMath::Abs(Position.Y) < HalfExtent.Y + 1000.0)
                    return TestTrue(TEXT("Grove silhouettes stay beyond the playable edge"), false);
            }
        }
        TestTrue(TEXT("Grove mesh instances stay within the rendering budget"),
            Instances > 0 && Instances <= 180 && Surroundings.Groves.Num() <= 6);
        if (Terrain == T::OpenField || Terrain == T::Waterloo || Terrain == T::Narva)
        {
            int32 LandmarkPieces = 0;
            for (const auto& Batch : Surroundings.Landmarks)
                for (const auto& Transform : Batch.Instances)
                {
                    ++LandmarkPieces;
                    const FVector P = Transform.GetLocation();
                    TestTrue(TEXT("Landmark pieces stay beyond the playable edge"),
                        FMath::Abs(P.X) > HalfExtent.X + 1000.0 || FMath::Abs(P.Y) > HalfExtent.Y + 1000.0);
                }
            TestTrue(TEXT("The landmark is visible geometry with a small mesh budget"),
                LandmarkPieces > 0 && LandmarkPieces <= 8);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegimentWaterlooRoofTest, "RegimentBattle.Presentation.WaterlooRoofs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FRegimentWaterlooRoofTest::RunTest(const FString&)
{
    const auto Batches = MakeTerrain(battle::MarchTerrain::Waterloo, {560, 380});
    const auto* Roofs = Batches.FindByPredicate([](const auto& Batch)
    {
        return Batch.Shape == EShape::Box && Batch.Color == EBoardColor::Orange;
    });
    if (!TestNotNull(TEXT("The three farms have roof panels"), Roofs) ||
        !TestEqual(TEXT("Waterloo has two panels per farm"), Roofs->Instances.Num(), 6))
        return false;

    for (int32 I = 0; I < Roofs->Instances.Num(); I += 2)
    {
        const auto& Left = Roofs->Instances[I];
        const auto& Right = Roofs->Instances[I + 1];
        const auto LeftRidge = Left.TransformPosition(FVector(0, 50, 0));
        const auto RightRidge = Right.TransformPosition(FVector(0, -50, 0));
        TestTrue(TEXT("Left panel rises toward the ridge"),
            LeftRidge.Z > Left.TransformPosition(FVector(0, -50, 0)).Z);
        TestTrue(TEXT("Right panel rises toward the ridge"),
            RightRidge.Z > Right.TransformPosition(FVector(0, 50, 0)).Z);
        TestTrue(TEXT("The two planes meet without a gap"), LeftRidge.Y >= RightRidge.Y);
    }
    return true;
}
#endif
