#include "RegimentAncientFigures.h"
#include "RegimentEraCatalog.h"
#include "RegimentFigureAppearance.h"

#include "Algo/Count.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

THIRD_PARTY_INCLUDES_START
#include "battle/Simulation.h"
THIRD_PARTY_INCLUDES_END

namespace
{
using P = ERegimentAncientPrimitive;
using M = ERegimentAncientMaterial;
using F = ERegimentAncientFeature;

void Part(FRegimentAncientFigure& Model, P Shape, M Surface, FVector Position, FVector Size,
    FRotator Rotation = FRotator::ZeroRotator, F Feature = F::None)
{
    const FTransform Transform(Rotation, Position, Size);
    Model.Parts.Add({Shape, Surface, Transform, Feature});
    Model.Bounds += FBox(FVector(-.5), FVector(.5)).TransformBy(Transform);
}

void Bar(FRegimentAncientFigure& Model, M Surface, FVector From, FVector To, double Thickness,
    P Shape = P::Block, F Feature = F::None)
{
    Part(Model, Shape, Surface, (From + To) * .5, {Thickness, Thickness, FVector::Distance(From, To)},
        FRotationMatrix::MakeFromZ(To - From).Rotator(), Feature);
}

void Soldier(FRegimentAncientFigure& Model, FVector Feet, double Yaw, F Role, bool bSeated = false)
{
    const FTransform Pose(FRotator(0, Yaw, 0), Feet);
    const double Seat = bSeated ? 0 : 61;
    const auto Body = [&](P Shape, M Surface, FVector Position, FVector Size, double Pitch = 0,
        F Feature = F::None)
    {
        Part(Model, Shape, Surface, Pose.TransformPositionNoScale(Position), Size, FRotator(Pitch, Yaw, 0),
            Feature);
    };
    for (double Side : {-1.0, 1.0})
    {
        Body(P::BeveledBlock, M::Leather, {bSeated ? 11.0 : 0.0, Side * 11, Seat * .5},
            {16, 15, bSeated ? 40.0 : 63.0}, bSeated ? 27 : -4);
        Body(P::BeveledBlock, M::Paint, {11, Side * 25, Seat + 38}, {20, 14, 47}, -33);
        Body(P::Ellipsoid, M::Skin, {32, Side * 25, Seat + 30}, {15, 13, 13});
    }
    Body(P::BeveledBlock, M::Paint, {0, 0, Seat + 33}, {31, 45, 62}, -5);
    Body(P::Block, M::Leather, {0, 0, Seat + 10}, {33, 45, 8});
    Body(P::Ellipsoid, M::Skin, {4, 0, Seat + 78}, {27, 24, 29}, 0, Role);
    Body(P::Ellipsoid, M::Bronze, {1, 0, Seat + 88}, {31, 28, 25});
    if (!bSeated)
        Body(P::BeveledBlock, M::Paint, {-4, 0, Seat + 102}, {25, 12, 13});
}

FRegimentAncientFigure Elephant(bool bAfrican)
{
    FRegimentAncientFigure Model;
    const double Size = bAfrican ? 1.19 : 1.0;
    const auto Add = [&](P Shape, M Surface, FVector Position, FVector Dimensions,
        FRotator Rotation = FRotator::ZeroRotator, F Feature = F::None)
    {
        Part(Model, Shape, Surface, Position * Size, Dimensions * Size, Rotation, Feature);
    };
    Add(P::Ellipsoid, M::Hide, {-30, 0, 167}, {236, 132, 181});
    Add(P::Ellipsoid, M::Hide, {50, 0, 169}, {121, 131, 190});
    Add(P::Ellipsoid, M::Hide, {100, 0, 182}, {102, 94, 106}, FRotator(-8, 0, 0));
    Add(P::Ellipsoid, M::Hide, {90, 0, 221}, {bAfrican ? 74.0 : 91.0, 77, bAfrican ? 46.0 : 55.0});
    for (double Side : {-1.0, 1.0})
    {
        for (double X : {-102.0, 53.0})
        {
            Add(P::BeveledBlock, M::Hide, {X, Side * 39, 96}, {52, 48, 130},
                FRotator(X < 0 ? -13 : 7, 0, 0));
            Add(P::BeveledBlock, M::Hide, {X + 6, Side * 40, 37}, {45, 41, 72},
                FRotator(X < 0 ? 8 : -3, 0, 0));
            Add(P::BeveledBlock, M::Dark, {X + 7, Side * 40, 9}, {51, 46, 18});
        }
        const double EarY = bAfrican ? 67 : 53;
        Add(P::Ellipsoid, M::Hide, {73, Side * EarY, 178},
            {bAfrican ? 103.0 : 65.0, 22, bAfrican ? 137.0 : 73.0},
            FRotator(-11, Side * 27, Side * -13), F::Ear);
        Add(P::Ellipsoid, M::Leather, {76, Side * (EarY + 7), 179},
            {bAfrican ? 69.0 : 40.0, 9, bAfrican ? 102.0 : 52.0},
            FRotator(-11, Side * 27, Side * -13));
        Add(P::Ellipsoid, M::Dark, {126, Side * 35, 196}, {12, 12, 11});
        const FVector Root(119, Side * 29, 151);
        const FVector Bend(bAfrican ? 148.0 : 145.0, Side * 42, 131);
        const FVector Tip(bAfrican ? 180.0 : 164.0, Side * 46, bAfrican ? 149.0 : 139.0);
        Bar(Model, M::Ivory, Root * Size, Bend * Size, 18 * Size, P::Disc);
        Bar(Model, M::Ivory, Bend * Size, Tip * Size, 18 * Size, P::Taper, F::Tusk);
        Add(P::BeveledBlock, M::Paint, {-24, Side * 65, 210}, {130, 12, 78},
            FRotator(0, 0, Side * 9));
        Add(P::Block, M::Bronze, {-24, Side * 71, 174}, {132, 9, 8});
        Add(P::Block, M::Leather, {-32, Side * 66, 217}, {14, 9, 100});
    }
    const FVector Trunk[] = {{137, 0, 166}, {144, 0, 128}, {144, 0, 87}, {156, 0, 63}, {173, 0, 69}};
    for (int32 Index = 1; Index < UE_ARRAY_COUNT(Trunk); ++Index)
    {
        Add(P::Ellipsoid, M::Hide, Trunk[Index - 1], {47 - 7.0 * Index, 47 - 7.0 * Index, 43 - 5.0 * Index});
        Bar(Model, M::Hide, Trunk[Index - 1] * Size, Trunk[Index] * Size,
            (43 - 6.0 * Index) * Size, P::Disc);
    }
    Bar(Model, M::Hide, FVector(-141, 0, 163) * Size, FVector(-162, 0, 99) * Size, 12 * Size, P::Disc);
    Add(P::Ellipsoid, M::Dark, {-164, 0, 93}, {14, 15, 24});
    Add(P::BeveledBlock, M::Bronze, {137, 0, 213}, {20, 62, 56}, FRotator(-17, 0, 0));
    Add(P::Ellipsoid, M::Paint, {148, 0, 220}, {11, 32, 33}, FRotator(-17, 0, 0));

    const double Deck = 256 * Size;
    Part(Model, P::BeveledBlock, M::Wood, {-34 * Size, 0, Deck + 4}, {141, 113, 16});
    for (double Side : {-1.0, 1.0})
    {
        Part(Model, P::Block, M::Paint, {-34 * Size, Side * 54, Deck + 30}, {141, 10, 47});
        Part(Model, P::Block, M::Bronze, {-34 * Size, Side * 55, Deck + 56}, {144, 12, 8});
        for (double X : {-101.0, -61.0, -20.0, 33.0})
            Part(Model, P::BeveledBlock, M::Wood, {X - 34 * (Size - 1), Side * 56, Deck + 36}, {8, 11, 64});
        Part(Model, P::Block, M::Paint, {-34 * Size + Side * 67, 0, Deck + 30}, {10, 113, 47});
        Part(Model, P::Block, M::Bronze, {-34 * Size + Side * 67, 0, Deck + 56}, {12, 116, 8});
    }
    Soldier(Model, {74 * Size, 0, 246 * Size}, 0, F::Driver, true);
    Soldier(Model, {-63 * Size, -23, Deck + 10}, -25, F::TowerCrew);
    Soldier(Model, {-11 * Size, 22, Deck + 10}, 22, F::TowerCrew);
    Bar(Model, M::Wood, {-17 * Size, 46, Deck + 105}, {24 * Size, 54, Deck + 151}, 6, P::Disc);
    Bar(Model, M::Iron, {24 * Size, 54, Deck + 151}, {39 * Size, 58, Deck + 170}, 8, P::Taper);
    return Model;
}

void Horse(FRegimentAncientFigure& Model, FVector Location, M Coat, double Stride, bool bHarness = true)
{
    const auto Add = [&](P Shape, M Surface, FVector Position, FVector Size, double Pitch = 0, F Feature = F::None)
    {
        Part(Model, Shape, Surface, Location + Position, Size, FRotator(Pitch, 0, 0), Feature);
    };
    Add(P::Ellipsoid, Coat, {-10, 0, 103}, {133, 50, 61}, 0, F::Horse);
    Add(P::Ellipsoid, Coat, {36, 0, 103}, {59, 53, 61});
    Add(P::BeveledBlock, Coat, {53, 0, 131}, {40, 36, 72}, 31);
    Add(P::BeveledBlock, M::Dark, {42, 0, 146}, {14, 26, 64}, 28);
    Add(P::BeveledBlock, Coat, {75, 0, 164}, {49, 30, 36}, -21);
    Add(P::BeveledBlock, M::Leather, {89, 0, 154}, {23, 31, 18}, -19);
    if (bHarness)
        Add(P::BeveledBlock, M::Bronze, {54, 0, 120}, {44, 52, 12}, 12);
    for (double Side : {-1.0, 1.0})
    {
        Add(P::Taper, Coat, {63, Side * 10, 185}, {14, 13, 26}, 13);
        Add(P::Ellipsoid, M::Dark, {78, Side * 15, 170}, {8, 6, 7});
        for (double X : {-54.0, 33.0})
        {
            const double Step = (X < 0 ? -1 : 1) * Side * Stride;
            const FVector Hock(X + Step * .5, Side * 17, 47);
            const FVector Foot(X + Step, Side * 17, 9);
            Bar(Model, Coat, Location + FVector(X, Side * 18, 92), Location + Hock, 18, P::BeveledBlock);
            Bar(Model, Coat, Location + Hock, Location + Foot, 13, P::BeveledBlock);
            Add(P::BeveledBlock, M::Dark, Foot, {23, 17, 16});
        }
    }
    Bar(Model, M::Dark, Location + FVector(-66, 0, 111), Location + FVector(-88, 0, 72), 16, P::BeveledBlock);
}

FRegimentAncientFigure Chariot()
{
    FRegimentAncientFigure Model;
    const double HorseY[] = {-99, -33, 33, 99};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(HorseY); ++Index)
    {
        const double X = Index == 0 || Index == 3 ? 176 : 189;
        const double Y = HorseY[Index];
        Horse(Model, {X, Y, 0}, Index == 0 || Index == 3 ? M::Leather : M::Hide, Index % 2 ? 20 : -24);
        Bar(Model, M::Leather, {-59, Y * .30, 165}, {X + 87, Y, 157}, 4, P::Disc);
    }
    Bar(Model, M::Wood, {-140, 0, 69}, {216, 0, 98}, 15, P::BeveledBlock);
    Bar(Model, M::Bronze, {201, -114, 98}, {201, 114, 98}, 12, P::Disc);
    Part(Model, P::BeveledBlock, M::Wood, {-110, 0, 65}, {123, 117, 22});
    Part(Model, P::BeveledBlock, M::Paint, {-54, 0, 101}, {18, 107, 64});
    Part(Model, P::Block, M::Bronze, {-54, 0, 134}, {19, 111, 8});
    for (double Side : {-1.0, 1.0})
    {
        Part(Model, P::Block, M::Paint, {-112, Side * 53, 82}, {118, 11, 22});
        Part(Model, P::Block, M::Bronze, {-112, Side * 53, 128}, {118, 11, 10});
        // The open spaces between these rails and posts are the cart's apertures.
        for (double X : {-166.0, -131.0, -92.0, -56.0})
            Part(Model, P::BeveledBlock, M::Paint, {X, Side * 53, 105}, {9, 11, 54});

        const FVector Hub(-114, Side * 68, 57);
        constexpr int32 Spokes = 12;
        for (int32 Index = 0; Index < Spokes; ++Index)
        {
            const auto Rim = [&](int32 Segment)
            {
                const double Angle = Segment * (2 * PI / Spokes);
                return Hub + FVector(FMath::Cos(Angle) * 49, 0, FMath::Sin(Angle) * 49);
            };
            Bar(Model, M::Bronze, Rim(Index), Rim(Index + 1), 10);
            if (Index % 2 == 0)
                Bar(Model, M::Wood, Hub, Rim(Index), 7);
        }
        Bar(Model, M::Iron, Hub - FVector(0, Side * 8, 0), Hub + FVector(0, Side * 26, 0), 24, P::Disc);
        const FVector Bend = Hub + FVector(14, Side * 47, 5);
        Bar(Model, M::Iron, Hub + FVector(0, Side * 19, 0), Bend, 16, P::BeveledBlock);
        Bar(Model, M::Iron, Bend, Hub + FVector(42, Side * 78, 9), 17, P::Taper, F::Scythe);
    }
    Soldier(Model, {-78, 13, 75}, 0, F::Driver);
    Soldier(Model, {-140, -17, 75}, -24, F::EngineCrew);
    Bar(Model, M::Wood, {-146, -44, 154}, {-102, -46, 232}, 6, P::Disc);
    Bar(Model, M::Iron, {-102, -46, 232}, {-92, -47, 255}, 10, P::Taper);
    return Model;
}

FRegimentAncientFigure Engine(bool bBallista)
{
    FRegimentAncientFigure Model;
    for (double Side : {-1.0, 1.0})
    {
        Part(Model, P::BeveledBlock, M::Wood, {0, Side * 44, 13}, {194, 24, 23});
        Bar(Model, M::Wood, {-79, Side * 44, 19}, {-18, Side * 38, bBallista ? 73.0 : 111.0}, 14);
        Bar(Model, M::Wood, {64, Side * 44, 19}, {16, Side * 36, bBallista ? 73.0 : 111.0}, 16);
        const FVector Axel(bBallista ? 28.0 : -9.0, Side * 41, bBallista ? 76.0 : 103.0);
        Bar(Model, M::Bronze, Axel - FVector(0, 13, 0), Axel + FVector(0, 13, 0), 28, P::Disc);
        Part(Model, P::Block, M::Iron, {63, Side * 44, 25}, {19, 27, 9});
    }
    for (double X : {-72.0, 61.0})
        Part(Model, P::BeveledBlock, M::Wood, {X, 0, 26}, {24, 106, 22});
    if (bBallista)
    {
        Part(Model, P::BeveledBlock, M::Wood, {22, 0, 83}, {193, 23, 19}, FRotator(10, 0, 0));
        for (double Side : {-1.0, 1.0})
        {
            const FVector Joint(43, Side * 42, 97);
            const FVector Elbow(19, Side * 82, 101);
            const FVector Tip(-29, Side * 106, 96);
            Bar(Model, M::Wood, Joint, Elbow, 18, P::BeveledBlock, F::BowArm);
            Bar(Model, M::Wood, Elbow, Tip, 14, P::BeveledBlock);
            Bar(Model, M::Ivory, Tip, {-50, 0, 99}, 4, P::Disc);
        }
        Bar(Model, M::Leather, {-46, 0, 87}, {111, 0, 111}, 6, P::Disc);
        Bar(Model, M::Iron, {107, 0, 110}, {131, 0, 114}, 13, P::Taper);
    }
    else
    {
        Bar(Model, M::Wood, {-30, 0, 40}, {45, 0, 207}, 20, P::BeveledBlock, F::ThrowingArm);
        Part(Model, P::BeveledBlock, M::Iron, {46, 0, 207}, {45, 40, 19}, FRotator(12, 0, 0));
        Part(Model, P::Ellipsoid, M::Hide, {49, 0, 221}, {33, 34, 28});
        Part(Model, P::BeveledBlock, M::Wood, {20, 0, 122}, {23, 100, 19});
        Bar(Model, M::Ivory, {-31, 0, 47}, {-70, 0, 89}, 5, P::Disc);
        for (double Y : {-110.0, -79.0})
            Part(Model, P::Ellipsoid, M::Hide, {55, Y, 17}, {31, 30, 33});
    }
    Soldier(Model, {-82, -81, 0}, 60, F::EngineCrew);
    Soldier(Model, {-51, 90, 0}, -58, F::EngineCrew);
    return Model;
}

FRegimentAncientFigure FieldGun()
{
    FRegimentAncientFigure Model;
    for (double Side : {-1.0, 1.0})
    {
        const FVector WheelCenter(22, Side * 59, 54);
        Bar(Model, M::Iron, WheelCenter - FVector(0, 5, 0), WheelCenter + FVector(0, 5, 0), 105, P::Disc);
        Bar(Model, M::Wood, WheelCenter - FVector(0, 8, 0), WheelCenter + FVector(0, 8, 0), 94, P::Disc);
        Bar(Model, M::Bronze, WheelCenter - FVector(0, 12, 0), WheelCenter + FVector(0, 12, 0), 26, P::Disc);
        for (int32 Spoke = 0; Spoke < 8; ++Spoke)
        {
            const double Angle = Spoke * UE_DOUBLE_PI / 8.0;
            const FVector Tip(44.0 * FMath::Cos(Angle), Side * 10, 44.0 * FMath::Sin(Angle));
            Bar(Model, M::Leather, WheelCenter + FVector(0, Side * 10, 0), WheelCenter + Tip, 6);
        }
        Bar(Model, M::Paint, {-126, Side * 21, 14}, {44, Side * 34, 68}, 20, P::BeveledBlock);
    }
    Bar(Model, M::Iron, {22, -61, 54}, {22, 61, 54}, 18, P::Disc);
    Bar(Model, M::Bronze, {-43, 0, 73}, {117, 0, 81}, 30, P::Disc);
    Bar(Model, M::Iron, {102, 0, 80}, {119, 0, 81}, 36, P::Disc);
    Bar(Model, M::Dark, {118, 0, 81}, {120, 0, 81}, 20, P::Disc);
    Soldier(Model, {-59, -94, 0}, 52, F::EngineCrew);
    Soldier(Model, {-74, 93, 0}, -52, F::EngineCrew);
    return Model;
}

// Convex primitives share normalized geometry. A face carries a flat normal and no seams depend on UVs.
struct FAncientPrimitiveMesh
{
    TArray<FVector> Points;
    TArray<FIntVector> Faces;

    void Fan(int32 First, int32 Sides)
    {
        for (int32 Index = 1; Index + 1 < Sides; ++Index)
            Faces.Emplace(First, First + Index, First + Index + 1);
    }

    void JoinRings(int32 First, int32 Second, int32 Sides)
    {
        for (int32 Index = 0; Index < Sides; ++Index)
        {
            const int32 Next = (Index + 1) % Sides;
            Faces.Emplace(First + Index, First + Next, Second + Next);
            Faces.Emplace(First + Index, Second + Next, Second + Index);
        }
    }
};

FAncientPrimitiveMesh Primitive(P Shape, bool bSimplified)
{
    FAncientPrimitiveMesh Result;
    if (Shape == P::Block || Shape == P::BeveledBlock)
    {
        const FVector2D Outline[] = {{-.5, -.5}, {.5, -.5}, {.5, .5}, {-.5, .5}};
        for (double Z : {-.5, .5})
        {
            if (bSimplified || Shape == P::Block)
                for (const FVector2D Corner : Outline)
                    Result.Points.Emplace(Corner.X, Corner.Y, Z);
            else
            {
                // Octagonal plan softens the silhouette and keeps joinery readable.
                const FVector2D Beveled[] = {{-.5, -.35}, {-.35, -.5}, {.35, -.5}, {.5, -.35},
                    {.5, .35}, {.35, .5}, {-.35, .5}, {-.5, .35}};
                for (const FVector2D Corner : Beveled)
                    Result.Points.Emplace(Corner.X, Corner.Y, Z);
            }
        }
        const int32 Sides = Result.Points.Num() / 2;
        Result.Fan(0, Sides);
        Result.JoinRings(0, Sides, Sides);
        Result.Fan(Sides, Sides);
        return Result;
    }
    const int32 RoundSides = bSimplified ? 6 : 10;
    const auto AddRing = [&](double Z, double Radius)
    {
        for (int32 Index = 0; Index < RoundSides; ++Index)
        {
            const double Angle = Index * (2 * PI / RoundSides);
            Result.Points.Emplace(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), Z);
        }
    };
    if (Shape == P::Ellipsoid)
    {
        AddRing(-.5, .23);
        if (!bSimplified)
            AddRing(-.24, .435);
        AddRing(0, .5);
        if (!bSimplified)
            AddRing(.24, .435);
        AddRing(.5, .23);
        for (int32 Ring = 0; Ring < (bSimplified ? 2 : 4); ++Ring)
            Result.JoinRings(Ring * RoundSides, (Ring + 1) * RoundSides, RoundSides);
    }
    else
    {
        AddRing(-.5, Shape == P::Frustum ? .39 : .5);
        AddRing(.5, Shape == P::Taper ? .035 : .5);
        Result.JoinRings(0, RoundSides, RoundSides);
    }
    Result.Fan(0, RoundSides);
    Result.Fan(Result.Points.Num() - RoundSides, RoundSides);
    return Result;
}
}

int32 FRegimentAncientFigure::Count(ERegimentAncientFeature Feature) const
{
    return Algo::CountIf(Parts, [Feature](const FRegimentAncientPart& Part) { return Part.Feature == Feature; });
}

FRegimentAncientFigure BuildRegimentAncientPlatform(battle::BattlePlatform Platform)
{
    switch (Platform)
    {
    case battle::BattlePlatform::IndianElephant: return Elephant(false);
    case battle::BattlePlatform::AfricanElephant: return Elephant(true);
    case battle::BattlePlatform::ScythedChariot: return Chariot();
    case battle::BattlePlatform::Catapult: return Engine(false);
    case battle::BattlePlatform::Ballista: return Engine(true);
    case battle::BattlePlatform::FieldGun: return FieldGun();
    default: return {};
    }
}

ERegimentAncientShield RegimentAncientShieldStyle(FStringView Faction)
{
    if (Faction == TEXT("rome"))
        return ERegimentAncientShield::Scutum;
    if (Faction == TEXT("persia"))
        return ERegimentAncientShield::Wicker;
    if (Faction == TEXT("gauls"))
        return ERegimentAncientShield::Oval;
    if (Faction == TEXT("carthage"))
        return ERegimentAncientShield::Carthaginian;
    return ERegimentAncientShield::Aspis;
}

double RegimentAncientPikeLength(FStringView Faction)
{
    return Faction == TEXT("macedon") || Faction == TEXT("diadochi") ? 383.0 : 242.0;
}

ERegimentAncientShield RegimentPaintedShieldStyle(ERegimentAncientShield Shield, FStringView Faction, uint64 Id)
{
    using H = ERegimentAncientShield;
    if (Shield != H::Aspis)
        return Shield;
    const uint32 Variant = (RegimentFigureHash(Id) >> 12) & 3;
    if (Faction == TEXT("sparta"))
        return Variant == 0 ? H::Aspis : H::AspisLambda;
    constexpr H Designs[] = {H::Aspis, H::AspisLambda, H::AspisRosette, H::AspisSun};
    return Designs[Variant];
}

double RegimentAncientSpearLength(double BaseLength, uint64 Id)
{
    const int32 Variation = int32((RegimentFigureHash(Id) >> 20) % 5) - 2;
    const double Step = BaseLength >= 400.0 ? .02 : .04;
    return BaseLength * (1.0 + Step * Variation);
}

FRegimentAncientFigure BuildRegimentAncientShield(ERegimentAncientShield Shield)
{
    FRegimentAncientFigure Model;
    const FRotator Facing(90, 0, 0);
    switch (Shield)
    {
    case ERegimentAncientShield::Scutum:
        for (double Y : {-17.0, 0.0, 17.0})
        {
            const double X = Y == 0 ? 5 : 1;
            const FRotator Arc(0, Y * .9, 0);
            Part(Model, P::BeveledBlock, M::Paint, {X, Y, 0}, {9, 21, 78}, Arc);
            for (double Z : {-38.0, 38.0})
                Part(Model, P::Block, M::Bronze, {X + 1, Y, Z}, {10, 21, 5}, Arc);
        }
        Part(Model, P::Ellipsoid, M::Bronze, {10, 0, 0}, {15, 19, 19});
        break;
    case ERegimentAncientShield::Wicker:
        Part(Model, P::BeveledBlock, M::Wicker, {0, 0, -10}, {10, 61, 104});
        Part(Model, P::BeveledBlock, M::Paint, {6, 0, 19}, {5, 62, 20});
        for (double Y : {-24.0, -12.0, 0.0, 12.0, 24.0})
            Part(Model, P::Block, M::Wood, {6, Y, -11}, {4, 3, 94});
        for (double Z : {-57.0, -37.0, -12.0, 40.0})
            Part(Model, P::Block, M::Leather, {8, 0, Z}, {4, 60, 7});
        break;
    case ERegimentAncientShield::Oval:
        Part(Model, P::Disc, M::Bronze, {0, 0, 0}, {85, 51, 8}, Facing);
        Part(Model, P::Ellipsoid, M::Paint, {4, 0, 0}, {78, 46, 13}, Facing);
        Part(Model, P::BeveledBlock, M::Wood, {10, 0, 0}, {9, 7, 70});
        Part(Model, P::Ellipsoid, M::Iron, {13, 0, 0}, {11, 15, 20});
        break;
    case ERegimentAncientShield::CavalryOval:
        Part(Model, P::Disc, M::Bronze, {0, 0, 0}, {68, 42, 7}, Facing);
        Part(Model, P::Ellipsoid, M::Paint, {5, 0, 0}, {63, 37, 12}, Facing);
        Part(Model, P::Ellipsoid, M::Iron, {12, 0, 0}, {11, 14, 18});
        for (double Z : {-14.0, 14.0})
            Part(Model, P::BeveledBlock, M::Bronze, {10, 0, Z}, {6, 24, 3});
        break;
    case ERegimentAncientShield::Macedonian:
        Part(Model, P::Disc, M::Bronze, {0, 0, 0}, {47, 47, 8}, Facing);
        Part(Model, P::Disc, M::Paint, {5, 0, 0}, {37, 37, 7}, Facing);
        Part(Model, P::Ellipsoid, M::Bronze, {11, 0, 0}, {10, 15, 15});
        break;
    case ERegimentAncientShield::Carthaginian:
        Part(Model, P::Disc, M::Leather, {0, 0, 0}, {58, 58, 9}, Facing);
        Part(Model, P::Disc, M::Paint, {5, 0, 0}, {53, 53, 9}, Facing);
        Part(Model, P::Ellipsoid, M::Bronze, {11, 0, 0}, {9, 13, 13});
        for (double Y : {-13.0, 13.0})
        {
            Bar(Model, M::Bronze, {11, Y, 9}, {11, Y * .6, -9}, 4);
            Part(Model, P::Ellipsoid, M::Bronze, {11, Y, 12}, {5, 7, 7});
        }
        break;
    case ERegimentAncientShield::Egyptian:
        Part(Model, P::BeveledBlock, M::Paint, {0, 0, -9}, {8, 44, 64});
        Part(Model, P::Disc, M::Paint, {0, 0, 20}, {34, 44, 8}, Facing);
        for (double Y : {-19., 19.})
            Bar(Model, M::Leather, {5, Y, -39}, {5, Y, 20}, 5);
        Part(Model, P::Disc, M::Bronze, {7, 0, 6}, {16, 16, 8}, Facing);
        break;
    case ERegimentAncientShield::RoundMedieval:
        Part(Model, P::Disc, M::Wood, {0, 0, 0}, {62, 62, 10}, Facing);
        Part(Model, P::Disc, M::Paint, {4, 0, 0}, {55, 55, 8}, Facing);
        Part(Model, P::Ellipsoid, M::Iron, {11, 0, 0}, {13, 18, 18});
        break;
    case ERegimentAncientShield::Kite:
        Part(Model, P::Taper, M::Wood, {0, 0, -9}, {10, 54, 65}, FRotator(0, 0, 180));
        Part(Model, P::Disc, M::Wood, {0, 0, 24}, {26, 55, 10}, Facing);
        Part(Model, P::Taper, M::Paint, {5, 0, -8}, {5, 47, 58}, FRotator(0, 0, 180));
        Part(Model, P::Disc, M::Paint, {5, 0, 22}, {23, 47, 4}, Facing);
        Part(Model, P::Ellipsoid, M::Iron, {10, 0, 13}, {9, 13, 13});
        break;
    case ERegimentAncientShield::Pavise:
        Part(Model, P::BeveledBlock, M::Wood, {0, 0, -4}, {11, 54, 89});
        Part(Model, P::BeveledBlock, M::Paint, {5, 0, -4}, {8, 48, 83});
        Part(Model, P::BeveledBlock, M::Leather, {10, 0, -2}, {6, 9, 77});
        Part(Model, P::BeveledBlock, M::Linen, {10, 0, 19}, {4, 34, 7});
        break;
    case ERegimentAncientShield::Aspis:
    case ERegimentAncientShield::AspisLambda:
    case ERegimentAncientShield::AspisRosette:
    case ERegimentAncientShield::AspisSun:
    {
        using H = ERegimentAncientShield;
        Part(Model, P::Disc, M::Bronze, {0, 0, 0}, {67, 67, 9}, Facing);
        Part(Model, P::Disc, M::Paint, {4, 0, 0}, {59, 59, 8}, Facing);
        if (Shield == H::Aspis)
            Part(Model, P::Ellipsoid, M::Bronze, {9, 0, 0}, {10, 14, 14});
        else if (Shield == H::AspisLambda)
        {
            Bar(Model, M::Linen, {10, -15, -16}, {10, 0, 18}, 6);
            Bar(Model, M::Linen, {10, 0, 18}, {10, 15, -16}, 6);
        }
        else if (Shield == H::AspisRosette)
        {
            Part(Model, P::Disc, M::Dark, {7, 0, 0}, {44, 44, 5}, Facing);
            for (int32 Petal = 0; Petal < 6; ++Petal)
            {
                const double Angle = Petal * PI / 3.0;
                const FVector Axis(0, FMath::Cos(Angle), FMath::Sin(Angle));
                Bar(Model, M::Linen, FVector(11, 0, 0) + Axis * 5, FVector(11, 0, 0) + Axis * 16, 6, P::Disc);
            }
            Part(Model, P::Disc, M::Ochre, {12, 0, 0}, {10, 10, 4}, Facing);
        }
        else
        {
            Part(Model, P::Disc, M::Ochre, {7, 0, 0}, {43, 43, 5}, Facing);
            for (int32 Ray = 0; Ray < 8; ++Ray)
            {
                const double Angle = Ray * PI / 4.0;
                const FVector Axis(0, FMath::Cos(Angle), FMath::Sin(Angle));
                Bar(Model, M::Dark, FVector(11, 0, 0) + Axis * 9, FVector(11, 0, 0) + Axis * 18, 3.5);
            }
            Part(Model, P::Disc, M::Dark, {12, 0, 0}, {12, 12, 4}, Facing);
        }
        break;
    }    }
    return Model;
}

ERegimentHistoricalFigure RegimentHistoricalProfile(FStringView Profile)
{
    using S = ERegimentHistoricalFigure;
    static constexpr struct { const TCHAR* Id; S Style; } Profiles[] = {
        {TEXT("legionaries"), S::Legionary}, {TEXT("auxilia"), S::Auxiliary},
        {TEXT("sarissa_phalanx"), S::Sarissa}, {TEXT("hoplites"), S::Hoplite},
        {TEXT("persian_sparabara"), S::Sparabara}, {TEXT("loose_bow"), S::LooseBow},
        {TEXT("loose_sling"), S::LooseSling}, {TEXT("loose_javelin"), S::LooseJavelin},
        {TEXT("fusiliers"), S::Fusilier}, {TEXT("grenadiers"), S::Grenadier},
        {TEXT("chasseurs"), S::Chasseur}, {TEXT("old_guard"), S::OldGuard}, {TEXT("young_guard"), S::YoungGuard},
        {TEXT("russian_line"), S::RussianLine}, {TEXT("russian_grenadiers"), S::RussianGrenadier},
        {TEXT("russian_jaegers"), S::RussianJaeger},
        {TEXT("british_line"), S::BritishLine}, {TEXT("british_grenadiers"), S::BritishGrenadier},
        {TEXT("british_light"), S::BritishLight}, {TEXT("highlanders"), S::Highlander},
    };
    for (const auto& Entry : Profiles)
        if (Profile == Entry.Id)
            return Entry.Style;
    return S::None;
}

ERegimentHistoricalFigure RegimentHistoricalStyle(ERegimentHistoricalFigure Profile, FStringView Faction,
    const battle::UnitSpec& Unit, FStringView Era)
{
    using S = ERegimentHistoricalFigure;
    using T = battle::UnitTroop;
    using W = battle::RangedWeapon;
    const bool bMounted = Unit.troop == T::Mounted;
    const bool bHeavy = Unit.cavalry == battle::CavalryClass::Heavy;
    const bool bAncient = Era == TEXT("ancient");
    const bool bNapoleonic = Era == TEXT("napoleonic");
    if (bAncient && bMounted)
    {
        if (Faction == TEXT("rome")) return S::RomanCavalry;
        if (Faction == TEXT("carthage") && Unit.cavalry == battle::CavalryClass::Light) return S::Numidian;
        if ((Faction == TEXT("macedon") || Faction == TEXT("diadochi")) && bHeavy) return S::Companion;
        if (Faction == TEXT("parthians") || Faction == TEXT("sarmatians") || Faction == TEXT("scythians"))
            return bHeavy ? S::Cataphract : S::SteppeRider;
        return bHeavy ? S::AncientHeavyHorse : S::AncientLightHorse;
    }
    if (bAncient && Unit.archer)
    {
        if (Unit.ranged_weapon == W::Sling) return S::LooseSling;
        if (Unit.ranged_weapon == W::Javelin) return S::LooseJavelin;
        if (Faction == TEXT("persia") && Unit.ranged_weapon == W::Bow) return S::PersianArcher;
        if (Unit.ranged_weapon == W::Bow)
        {
            if (Profile == S::LooseBow) return S::LooseBow;
            if (Faction == TEXT("egypt")) return S::Egyptian;
            if (Faction == TEXT("porus")) return S::Indian;
            if (Faction == TEXT("scythians") || Faction == TEXT("sarmatians")) return S::Steppe;
            if (Faction == TEXT("parthians")) return S::Iranian;
            return S::LooseBow;
        }
    }
    if (!bMounted && Profile != S::None && (bAncient || bNapoleonic))
        return Profile;
    if (bAncient)
    {
        if (Faction == TEXT("rome")) return Unit.troop == T::Spearman ? S::Auxiliary : S::Legionary;
        if (Faction == TEXT("sparta") || Faction == TEXT("greece") || Faction == TEXT("athens") ||
            Faction == TEXT("greeks")) return S::Hoplite;
        if ((Faction == TEXT("macedon") || Faction == TEXT("diadochi")) && Unit.troop == T::Spearman)
            return S::Sarissa;
        if (Faction == TEXT("persia")) return S::Sparabara;
    }
    if (bNapoleonic)
    {
        if (bMounted)
        {
            if (bHeavy) return S::NapoleonicCuirassier;
            return Faction == TEXT("poland") ? S::NapoleonicLancer : S::NapoleonicHussar;
        }
        if (Faction == TEXT("france")) return S::Fusilier;
        if (Faction == TEXT("russia")) return S::RussianLine;
        if (Faction == TEXT("britain")) return S::BritishLine;
        if (Faction == TEXT("poland")) return S::NapoleonicPolish;
        if (Faction == TEXT("austria")) return S::NapoleonicAustrian;
        if (Faction == TEXT("spain") || Faction == TEXT("portugal")) return S::NapoleonicIberian;
        return S::NapoleonicCentral;
    }
    if (bAncient)
    {
        if (Faction == TEXT("egypt")) return S::Egyptian;
        if (Faction == TEXT("porus")) return S::Indian;
        if (Faction == TEXT("carthage")) return S::Punic;
        if (Faction == TEXT("gauls")) return S::Gallic;
        if (Faction == TEXT("scythians") || Faction == TEXT("sarmatians")) return S::Steppe;
        if (Faction == TEXT("parthians")) return S::Iranian;
        if (Faction == TEXT("macedon") || Faction == TEXT("diadochi")) return S::Hellenistic;
    }
    if (Era == TEXT("high_medieval"))
    {
        const bool bSteppe = Faction == TEXT("mongols") || Faction == TEXT("golden_horde");
        const bool bEastern = Faction == TEXT("saracens") || Faction == TEXT("byzantium") || Faction == TEXT("rus");
        if (bMounted) return bSteppe || bEastern ? S::MedievalSteppeHorse : S::MedievalKnight;
        if (bSteppe) return S::SteppeMedieval;
        return bEastern ? S::EasternMail : S::WesternMail;
    }
    if (Era == TEXT("late_medieval"))
    {
        if (Faction == TEXT("ottomans"))
            return bMounted ? S::MedievalSteppeHorse : S::EasternMail;
        if (bMounted) return S::LateKnight;
        if (Faction == TEXT("swiss") && Unit.troop == T::Spearman) return S::SwissPike;
        if (Faction == TEXT("hussites")) return S::PaviseInfantry;
        return Unit.archer ? S::MedievalBowman : S::GothicPlate;
    }
    if (Era == TEXT("renaissance"))
    {
        if (bMounted && Faction == TEXT("commonwealth") && Unit.cavalry == battle::CavalryClass::Heavy)
            return S::WingedHussar;
        if (bMounted) return S::EarlyModernHorse;
        if (Faction == TEXT("russian_tsardom") || Faction == TEXT("commonwealth") ||
            Faction == TEXT("cossacks") || Faction == TEXT("ottomans"))
            return S::EasternEarlyModern;
        return Unit.archer ? S::PikeAndShotMissile : S::PikeAndShot;
    }
    if (Era == TEXT("great_northern_war"))
        return bMounted ? S::NorthernHorse : S::NorthernInfantry;
    return S::None;
}

ERegimentHistoricalFigure RegimentSengokuStyle(const battle::UnitSpec& Unit)
{
    using S = ERegimentHistoricalFigure;
    using T = battle::UnitTroop;
    if (Unit.troop == T::Mounted)
        return S::SengokuCavalry;
    if (Unit.archer)
        return battle::UsesGunpowder(Unit.ranged_weapon) ? S::SengokuArquebusier : S::SengokuArcher;
    return Unit.troop == T::Spearman ? S::SengokuAshigaru : S::SengokuSamurai;
}

TOptional<ERegimentAncientShield> RegimentHistoricalShield(ERegimentHistoricalFigure Style)
{
    using S = ERegimentHistoricalFigure;
    using H = ERegimentAncientShield;
    switch (Style)
    {
    case S::Legionary: return H::Scutum;
    case S::Auxiliary: return H::Oval;
    case S::RomanCavalry: return H::CavalryOval;
    case S::Sarissa: return H::Macedonian;
    case S::Hoplite: return H::Aspis;
    case S::Sparabara: return H::Wicker;
    case S::Egyptian: return H::Egyptian;
    case S::Punic: return H::Carthaginian;
    case S::Gallic: case S::Indian: return H::Oval;
    case S::Hellenistic: return H::Macedonian;
    case S::Steppe: case S::Iranian: case S::SteppeRider: case S::AncientLightHorse:
    case S::AncientHeavyHorse: return H::CavalryOval;
    case S::PaviseInfantry: return H::Pavise;
    default: return {};
    }
}

TOptional<ERegimentAncientShield> RegimentHistoricalShield(ERegimentHistoricalFigure Style,
    ERegimentEraShield FactionShield)
{
    using S = ERegimentHistoricalFigure;
    using H = ERegimentAncientShield;
    switch (Style)
    {
    case S::WesternMail: case S::EasternMail: case S::SteppeMedieval:
    case S::MedievalKnight: case S::MedievalSteppeHorse: case S::GothicPlate:
        if (FactionShield == ERegimentEraShield::Kite) return H::Kite;
        if (FactionShield == ERegimentEraShield::Rectangular) return H::Pavise;
        return H::RoundMedieval;
    default:
        return RegimentHistoricalShield(Style);
    }
}

double RegimentHistoricalSpearLength(ERegimentHistoricalFigure Style)
{
    using S = ERegimentHistoricalFigure;
    switch (Style)
    {
    case S::Sarissa: return 480.0;
    case S::Companion: return 280.0;
    case S::RomanCavalry: return 225.0;
    case S::Auxiliary: return 220.0;
    case S::Hoplite: return 268.0;
    case S::Sparabara: case S::Egyptian: return 191.0;
    case S::Indian: case S::Punic: return 254.0;
    case S::Hellenistic: return 265.0;
    case S::SteppeRider: case S::AncientLightHorse: return 208.0;
    case S::AncientHeavyHorse: return 287.0;
    case S::Cataphract: return 344.0;
    case S::SwissPike: return 408.0;
    case S::PikeAndShot: return 310.0;
    case S::WingedHussar: return 420.0;
    case S::SengokuAshigaru: return 282.0;
    case S::SengokuCavalry: return 255.0;
    default: return 242.0;
    }
}

namespace
{
using HistoricalFigure = ERegimentHistoricalFigure;

bool IsRefreshedFigure(HistoricalFigure Style)
{
    return Style >= HistoricalFigure::Egyptian;
}

bool IsHistoricalRider(HistoricalFigure Style)
{
    using S = HistoricalFigure;
    switch (Style)
    {
    case S::RomanCavalry: case S::Numidian: case S::Companion: case S::SengokuCavalry:
    case S::AncientLightHorse: case S::AncientHeavyHorse: case S::SteppeRider: case S::Cataphract:
    case S::MedievalKnight: case S::MedievalSteppeHorse: case S::LateKnight:
    case S::WingedHussar: case S::EarlyModernHorse: case S::NorthernHorse:
    case S::NapoleonicHussar: case S::NapoleonicCuirassier: case S::NapoleonicLancer:
        return true;
    default:
        return false;
    }
}

enum class EFieldHelmet : uint8
{
    Headcloth, WrappedCap, Crest, Conical, Fur, GreatHelm, Sallet, Felt, Morion, Lobster,
    CockedHat, Shako, Czapka, CrestedHelmet, Busby
};
enum class EFieldCoat : uint8 { Linen, Tunic, Coat, Mail, Lamellar, Plate, Braided };

struct FFieldOutfit
{
    EFieldHelmet Helmet;
    EFieldCoat Coat;
    M Trim;
};

FFieldOutfit FieldOutfit(HistoricalFigure Style)
{
    using S = HistoricalFigure;
    using H = EFieldHelmet;
    using C = EFieldCoat;
    switch (Style)
    {
    case S::Egyptian: return {H::Headcloth, C::Linen, M::Ochre};
    case S::Indian: return {H::WrappedCap, C::Linen, M::Scarlet};
    case S::Punic: return {H::Crest, C::Lamellar, M::Bronze};
    case S::Gallic: return {H::Conical, C::Tunic, M::Green};
    case S::Hellenistic: return {H::Crest, C::Linen, M::Bronze};
    case S::Steppe: case S::SteppeRider: return {H::Fur, C::Tunic, M::Leather};
    case S::Iranian: return {H::WrappedCap, C::Lamellar, M::Ochre};
    case S::AncientLightHorse: return {H::Conical, C::Tunic, M::Ochre};
    case S::AncientHeavyHorse: return {H::Crest, C::Lamellar, M::Bronze};
    case S::Cataphract: return {H::Conical, C::Lamellar, M::Iron};
    case S::WesternMail: return {H::Conical, C::Mail, M::Linen};
    case S::EasternMail: return {H::Conical, C::Lamellar, M::Bronze};
    case S::SteppeMedieval: case S::MedievalSteppeHorse: return {H::Fur, C::Lamellar, M::Leather};
    case S::MedievalKnight: return {H::GreatHelm, C::Mail, M::Iron};
    case S::GothicPlate: case S::LateKnight: return {H::Sallet, C::Plate, M::Iron};
    case S::MedievalBowman: case S::PaviseInfantry: return {H::Felt, C::Tunic, M::Linen};
    case S::SwissPike: return {H::Sallet, C::Tunic, M::Scarlet};
    case S::PikeAndShot: return {H::Morion, C::Plate, M::Linen};
    case S::PikeAndShotMissile: return {H::Felt, C::Coat, M::Leather};
    case S::EasternEarlyModern: return {H::Fur, C::Coat, M::Ochre};
    case S::WingedHussar: case S::EarlyModernHorse: return {H::Lobster, C::Plate, M::Bronze};
    case S::NorthernInfantry: return {H::CockedHat, C::Coat, M::Ivory};
    case S::NorthernHorse: return {H::CockedHat, C::Coat, M::Ochre};
    case S::NapoleonicPolish: return {H::Czapka, C::Coat, M::Scarlet};
    case S::NapoleonicAustrian: return {H::CrestedHelmet, C::Coat, M::Linen};
    case S::NapoleonicIberian: return {H::Shako, C::Coat, M::Linen};
    case S::NapoleonicHussar: return {H::Busby, C::Braided, M::Ochre};
    case S::NapoleonicCuirassier: return {H::CrestedHelmet, C::Plate, M::Bronze};
    case S::NapoleonicLancer: return {H::Czapka, C::Braided, M::Scarlet};
    default: return {H::Shako, C::Coat, M::Ochre};
    }
}

FRegimentAncientFigure RefreshedDress(HistoricalFigure Style, bool bSimplified)
{
    using H = EFieldHelmet;
    using C = EFieldCoat;
    using S = HistoricalFigure;
    const auto Outfit = FieldOutfit(Style);
    FRegimentAncientFigure Model;
    Model.bSimplified = bSimplified;
    const auto Add = [&](P Shape, M Surface, FVector At, FVector Size, FRotator Rotation = FRotator::ZeroRotator)
    {
        Part(Model, Shape, Surface, At, Size, Rotation);
    };

    const auto Helmet = Outfit.Helmet;
    if (Helmet == H::Headcloth || Helmet == H::WrappedCap || Helmet == H::Fur)
    {
        const bool bFur = Helmet == H::Fur;
        Add(P::Ellipsoid, bFur ? M::Leather : M::Linen, {-2, 0, 161}, {38, 41, 28});
        Add(P::Disc, Outfit.Trim, {-1, 0, 156}, {39, 42, 8}, FRotator(-7, 0, 0));
        if (Helmet == H::Headcloth)
            for (double Y : {-16., 16.})
                Add(P::BeveledBlock, M::Linen, {-8, Y, 145}, {21, 8, 26}, FRotator(-12, 0, Y));
        else
            Add(P::Ellipsoid, bFur ? M::Paint : Outfit.Trim, {-5, 0, 174}, {26, 27, bFur ? 25. : 15.});
    }
    else if (Helmet == H::Crest || Helmet == H::CrestedHelmet || Helmet == H::Morion)
    {
        const M Shell = Helmet == H::Crest ? M::Bronze : M::Iron;
        Add(P::Ellipsoid, Shell, {-3, 0, 162}, {38, 36, 30});
        Add(P::Disc, Shell, {0, 0, 154}, Helmet == H::Morion ? FVector(59, 43, 6) : FVector(43, 37, 5),
            FRotator(-8, 0, 0));
        Add(P::Ellipsoid, Helmet == H::Morion ? M::Iron : M::Dark, {-5, 0, 178}, {46, 9, 22},
            FRotator(-13, 0, 0));
        if (Style == S::NapoleonicCuirassier)
            Add(P::BeveledBlock, M::Dark, {-23, 0, 154}, {13, 13, 47}, FRotator(-17, 0, 0));
    }
    else if (Helmet == H::GreatHelm || Helmet == H::Sallet || Helmet == H::Lobster || Helmet == H::Conical)
    {
        if (Helmet == H::GreatHelm)
            Add(P::Frustum, M::Iron, {3, 0, 155}, {39, 39, 43});
        else if (Helmet == H::Conical)
            Add(P::Taper, M::Iron, {-1, 0, 172}, {39, 38, 32});
        else
            Add(P::Ellipsoid, M::Iron, {-2, 0, 159}, {45, 37, 32}, FRotator(-9, 0, 0));
        if (Helmet == H::GreatHelm || Helmet == H::Sallet)
            Add(P::BeveledBlock, M::Dark, {23, 0, 157}, {5, 27, 4}, FRotator(3, 0, 0));
        else
            Add(P::BeveledBlock, M::Iron, {16, 0, 153}, {7, 5, 22});
        if (Helmet != H::GreatHelm)
            Add(P::BeveledBlock, M::Iron, {-16, 0, 147}, {22, 37, 14}, FRotator(-19, 0, 0));
    }
    else if (Helmet == H::Felt || Helmet == H::CockedHat)
    {
        Add(P::Disc, M::Dark, {-1, 0, 156}, {56, 53, 5}, FRotator(-6, 0, 0));
        Add(P::Frustum, M::Dark, {-2, 0, 165}, {35, 37, 21});
        if (Helmet == H::CockedHat)
            for (double Angle : {0., 120., 240.})
            {
                const FRotator Turn(0, Angle, 0);
                Add(P::BeveledBlock, M::Dark, Turn.RotateVector({18, 0, 163}), {12, 38, 16}, Turn);
                if (!bSimplified)
                    Add(P::BeveledBlock, Outfit.Trim, Turn.RotateVector({21, 0, 171}), {3, 32, 3}, Turn);
            }
    }
    else
    {
        Add(P::Frustum, M::Dark, {-1, 0, 171}, {35, 37, 33});
        Add(P::Disc, M::Dark, {13, 0, 154}, {31, 38, 5});
        if (Helmet == H::Czapka)
            Add(P::BeveledBlock, M::Paint, {-3, 0, 189}, {37, 37, 8}, FRotator(0, 45, 0));
        else if (Helmet == H::Busby)
            Add(P::Ellipsoid, M::Paint, {-4, 9, 180}, {19, 18, 34}, FRotator(0, 0, 17));
        else
            Add(P::Disc, Outfit.Trim, {0, 0, 185}, {36, 37, 5});
        Add(P::BeveledBlock, Outfit.Trim, {17, 0, 170}, {5, 12, 12});
    }

    const auto Coat = Outfit.Coat;
    const bool bPlate = Coat == C::Plate;
    const bool bMail = Coat == C::Mail;
    const bool bLamellar = Coat == C::Lamellar;
    const bool bLong = Coat == C::Coat || Coat == C::Braided;
    const bool bMounted = IsHistoricalRider(Style);
    const M Cloth = Coat == C::Linen ? M::Linen : M::Paint;
    Add(P::BeveledBlock, bMail ? M::GreyWool : Cloth, {0, 0, 105}, {38, 49, 49});
    if (bPlate || bLamellar)
        Add(P::BeveledBlock, bPlate ? M::Iron : Outfit.Trim, {11, 0, 109}, {24, 49, 42}, FRotator(-7, 0, 0));
    if (bLamellar)
        for (double Z : {93., 108., 123.})
            Add(P::BeveledBlock, M::Dark, {23, 0, Z}, {6, 43, 3});
    if (bMail && !bSimplified)
        for (double Y : {-16., 16.})
            Add(P::BeveledBlock, M::GreyWool, {-9, Y, 142}, {28, 16, 28});
    if (bLong || bMail || bPlate)
        for (double Y : {-13., 13.})
            Add(P::BeveledBlock, bLong || bMail ? M::Paint : M::Leather,
                {-3, Y, bMounted ? 76. : 71.}, {35, 23, bMounted ? 21. : 29.}, FRotator(11, 0, Y * .3));
    Add(P::BeveledBlock, M::Leather, {0, 0, 84}, {40, 51, 7});
    Add(P::BeveledBlock, Outfit.Trim, {7, 0, 128}, {33, 32, 7});
    if (bPlate || bMail)
        for (double Y : {-23., 23.})
            Add(P::BeveledBlock, bPlate ? M::Iron : M::GreyWool, {0, Y, 120}, {31, 12, 12},
                FRotator(0, 0, Y * .7));

    if (Coat == C::Braided)
        for (double Z : {93., 104., 115., 124.})
            Add(P::BeveledBlock, Outfit.Trim, {21, 0, Z}, {4, 31, 3});
    else
    {
        const M Strap = bLong ? M::Linen : M::Leather;
        Bar(Model, Strap, {22, -16, 124}, {22, 18, 87}, 5);
        if (!bSimplified)
            Add(P::BeveledBlock, M::Leather, {-12, 24, 85}, {22, 12, 18});
    }

    if (Style == S::WingedHussar)
        for (double Y : {-24., 24.})
        {
            Bar(Model, M::Leather, {-23, Y, 88}, {-39, Y, 176}, 7);
            for (double Z : {120., 141., 162., 183.})
                Add(P::BeveledBlock, M::Linen, {-35 - (Z - 120) * .19, Y, Z}, {24, 7, 24},
                    FRotator(-32, 0, 0));
        }
    return Model;
}

bool IsSengokuFigure(HistoricalFigure Style)
{
    return Style >= HistoricalFigure::SengokuSamurai && Style <= HistoricalFigure::SengokuCavalry;
}

FRegimentAncientFigure SengokuDress(HistoricalFigure Style, bool bSimplified)
{
    using S = HistoricalFigure;
    const bool bMounted = Style == S::SengokuCavalry;
    const bool bArmoured = Style == S::SengokuSamurai || bMounted;
    FRegimentAncientFigure Model;
    Model.bSimplified = bSimplified;
    Part(Model, P::BeveledBlock, M::Dark, {0, 0, 107}, {37, 50, 53});
    Part(Model, P::BeveledBlock, M::Leather, {1, 0, 81}, {41, 53, 7});
    const int32 Plates = bArmoured ? (bSimplified ? 3 : 5) : 2;
    for (int32 Plate = 0; Plate < Plates; ++Plate)
    {
        const double Height = bArmoured ? 7.0 : 10.0;
        const double Z = 89.0 + Plate * (bSimplified ? 15.0 : bArmoured ? 8.0 : 14.0);
        Part(Model, P::BeveledBlock, M::Paint, {1, 0, Z}, {40, 52, Height},
            FRotator::ZeroRotator, F::Lamellar);
        if (bArmoured && !bSimplified)
            Part(Model, P::Block, M::Linen, {21, 0, Z - 2}, {2.5, 34, 1.5});
    }
    if (bArmoured)
    {
        Part(Model, P::Ellipsoid, M::Dark, {-2, 0, 163}, {38, 36, 28}, FRotator::ZeroRotator, F::Kabuto);
        Part(Model, P::Disc, M::Iron, {0, 0, 153}, {45, 49, 5});
        for (double Z : {143., 149.})
            Part(Model, P::BeveledBlock, M::Paint, {-12, 0, Z}, {24, 43 + (149. - Z) * .8, 6}, FRotator(-10, 0, 0));
        Part(Model, P::BeveledBlock, M::Dark, {15, 0, 140}, {12, 26, 13}, FRotator(-8, 0, 0));
        // A compact frontal crest leaves the head silhouette distinct from the broad ashigaru brim.
        Part(Model, P::Disc, M::Bronze, {17, 0, 168}, {13, 13, 3}, FRotator(90, 0, 0));
        for (double Side : {-1.0, 1.0})
        {
            if (bMounted)
                Bar(Model, M::Bronze, {17, Side * 3, 168}, {16, Side * 14, 181}, 5, P::Taper);
            Part(Model, P::BeveledBlock, M::Paint, {-1, Side * 27, 115}, {29, 9, 32},
                FRotator(0, 0, Side * 19), F::Sode);
            if (!bSimplified)
                for (double Z : {108., 116., 124.})
                    Part(Model, P::Block, M::Dark, {-1, Side * 31, Z}, {28, 3, 2}, FRotator(0, 0, Side * 19));
            for (double X : {-14., 16.})
                Part(Model, P::BeveledBlock, M::Paint, {X, Side * 14, 70}, {11, 22, 25},
                    FRotator(X > 0 ? 21 : -21, 0, Side * 12));
        }
        Bar(Model, M::Dark, {22, -24, 83}, {-37, -24, 73}, 7, P::Disc);
    }
    else
    {
        Part(Model, P::Taper, M::Dark, {1, 0, 161}, {59, 59, 21}, FRotator(-5, 0, 0), F::Jingasa);
        Part(Model, P::Disc, M::Leather, {0, 0, 151}, {58, 57, 4}, FRotator(-5, 0, 0));
        Part(Model, P::BeveledBlock, M::Linen, {16, 0, 124}, {9, 35, 11});
        for (double Side : {-1.0, 1.0})
            Part(Model, P::BeveledBlock, M::Paint, {16, Side * 13, 70}, {8, 20, 23}, FRotator(17, 0, Side * 7));
        if (Style == S::SengokuArcher)
        {
            Bar(Model, M::Leather, {-23, 16, 84}, {-26, 24, 145}, 12, P::Disc, F::Quiver);
            for (double Y : {21., 25., 29.})
                Bar(Model, M::Wood, {-26, Y, 139}, {-28, Y + 1, 157}, 3);
        }
        else if (Style == S::SengokuArquebusier)
        {
            Bar(Model, M::Leather, {21, -16, 123}, {22, 18, 84}, 4);
            Part(Model, P::BeveledBlock, M::Leather, {9, 25, 82}, {17, 10, 20});
        }
    }
    return Model;
}
}

FRegimentAncientFigure BuildRegimentHistoricalDress(ERegimentHistoricalFigure Style)
{
    using S = ERegimentHistoricalFigure;
    if (IsSengokuFigure(Style))
        return SengokuDress(Style, false);
    if (IsRefreshedFigure(Style))
        return RefreshedDress(Style, false);
    FRegimentAncientFigure Model;
    const auto Add = [&](P Shape, M Surface, FVector At, FVector Size, FRotator Rotation = FRotator::ZeroRotator,
        F Feature = F::None)
    {
        Part(Model, Shape, Surface, At, Size, Rotation, Feature);
    };
    const auto Helmet = [&](M Metal, bool bCrest)
    {
        Add(P::Ellipsoid, Metal, {-2, 0, 157}, {37, 34, 27});
        Add(P::BeveledBlock, Metal, {-15, 0, 144}, {23, 37, 7}, FRotator(-13, 0, 0));
        for (double Side : {-1., 1.})
            Add(P::BeveledBlock, Metal, {1, Side * 13, 144}, {18, 8, 21}, FRotator(-15, 0, Side * 13));
        if (bCrest)
            Add(P::Ellipsoid, M::Paint, {-4, 0, 174}, {38, 9, 17}, FRotator(-12, 0, 0));
    };
    const auto SoftCap = [&](M Cloth)
    {
        Add(P::Ellipsoid, Cloth, {-3, 0, 155}, {36, 35, 23});
        Add(P::Taper, Cloth, {-6, 0, 167}, {32, 30, 26}, FRotator(-29, 0, 0));
        Add(P::BeveledBlock, Cloth, {-12, 0, 145}, {15, 33, 25});
    };

    switch (Style)
    {
    case S::None:
        return Model;
    case S::Legionary:
        Helmet(M::Iron, false);
        Add(P::BeveledBlock, M::Iron, {0, 0, 108}, {39, 51, 43});
        for (double Z : {89., 98., 107., 116.})
            Add(P::BeveledBlock, M::Iron, {0, 0, Z}, {41, 53, 7});
        for (double Y : {-20., 20.})
            Add(P::BeveledBlock, M::Iron, {0, Y, 125}, {39, 18, 8});
        Add(P::BeveledBlock, M::Leather, {0, 0, 84}, {40, 49, 7});
        for (double Y : {-15., -5., 5., 15.})
            Add(P::Block, M::Leather, {21, Y, 72}, {5, 6, 24}, FRotator(8, 0, 0));
        return Model;
    case S::Auxiliary:
    case S::RomanCavalry:
        Helmet(Style == S::RomanCavalry ? M::Bronze : M::Iron, Style == S::RomanCavalry);
        Add(P::BeveledBlock, M::Iron, {0, 0, 103}, {37, 50, 49});
        Add(P::BeveledBlock, M::GreyWool, {-3, 0, 113}, {39, 52, 25});
        Add(P::BeveledBlock, M::Leather, {0, 0, 84}, {41, 53, 7});
        if (Style == S::RomanCavalry)
            Add(P::BeveledBlock, M::Paint, {-16, 0, 110}, {16, 34, 36}, FRotator(-12, 0, 0));
        return Model;
    case S::Hoplite:
    case S::Sarissa:
        Helmet(M::Bronze, true);
        if (Style == S::Hoplite)
        {
            Add(P::BeveledBlock, M::Bronze, {16, 0, 149}, {7, 5, 19});
            Add(P::BeveledBlock, M::Bronze, {0, 0, 107}, {41, 52, 46});
        }
        else
        {
            Add(P::BeveledBlock, M::Linen, {0, 0, 107}, {42, 53, 48});
            for (double Y : {-11., 11.})
                Add(P::BeveledBlock, M::Ochre, {19, Y, 114}, {5, 8, 36});
        }
        for (double Y : {-15., -5., 5., 15.})
            Add(P::BeveledBlock, M::Linen, {21, Y, 80}, {5, 7, 22}, FRotator(9, 0, 0));
        return Model;
    case S::Companion:
        // Boeotian helmet with a projecting brim and an unshielded lance arm.
        Add(P::Disc, M::Bronze, {0, 0, 153}, {51, 47, 7}, FRotator(-8, 0, 0));
        Add(P::Ellipsoid, M::Bronze, {-2, 0, 162}, {33, 34, 25});
        Add(P::BeveledBlock, M::Linen, {0, 0, 108}, {45, 54, 50});
        Add(P::BeveledBlock, M::Paint, {-17, 0, 110}, {20, 45, 56}, FRotator(-15, 0, 0));
        Add(P::Disc, M::Bronze, {22, -14, 120}, {11, 11, 4}, FRotator(90, 0, 0));
        return Model;
    case S::Sparabara:
    case S::PersianArcher:
        SoftCap(Style == S::PersianArcher ? M::Ochre : M::Paint);
        Add(P::BeveledBlock, M::Paint, {0, 0, 104}, {40, 52, 51});
        Add(P::BeveledBlock, M::Ochre, {0, 0, 84}, {42, 53, 8});
        Bar(Model, M::Ochre, {22, -21, 124}, {22, 20, 90}, 5);
        if (Style == S::PersianArcher)
        {
            Bar(Model, M::Leather, {-24, 15, 82}, {-30, -18, 141}, 16, P::Disc, F::Quiver);
            for (double Y : {-21., -17., -13.})
                Bar(Model, M::Wood, {-29, Y, 133}, {-32, Y - 4, 153}, 3);
        }
        return Model;
    case S::Numidian:
        Add(P::Ellipsoid, M::Dark, {-5, 0, 154}, {32, 31, 19});
        Add(P::Disc, M::Linen, {-2, 0, 157}, {34, 34, 7});
        Add(P::BeveledBlock, M::Linen, {0, 0, 106}, {38, 50, 51});
        Bar(Model, M::Leather, {20, -19, 124}, {19, 20, 89}, 5);
        return Model;
    case S::LooseBow:
    case S::LooseSling:
    case S::LooseJavelin:
        if (Style == S::LooseSling)
            Add(P::Disc, M::Linen, {0, 0, 156}, {34, 35, 8});
        else
            SoftCap(Style == S::LooseJavelin ? M::Linen : M::Paint);
        Add(P::BeveledBlock, M::Linen, {0, 0, 103}, {37, 47, 43});
        Add(P::BeveledBlock, M::Paint, {0, 0, 88}, {39, 48, 7});
        if (Style == S::LooseJavelin)
            for (double Y : {-16., -10.})
                Bar(Model, M::Wood, {-21, Y + 15, 72}, {-24, Y, 161}, 4, P::Disc);
        return Model;
    default:
        break;
    }

    const bool bRussian = Style == S::RussianLine || Style == S::RussianGrenadier || Style == S::RussianJaeger;
    const bool bBritish = Style == S::BritishLine || Style == S::BritishGrenadier ||
        Style == S::BritishLight || Style == S::Highlander;
    const M Coat = bRussian ? M::Green : bBritish ? M::Scarlet : M::Navy;
    const bool bLight = Style == S::Chasseur || Style == S::RussianJaeger || Style == S::BritishLight;
    const bool bGrenadier = Style == S::Grenadier || Style == S::RussianGrenadier || Style == S::BritishGrenadier;
    const bool bOldGuard = Style == S::OldGuard;
    const bool bHighlander = Style == S::Highlander;
    const bool bTallShako = Style == S::YoungGuard || bGrenadier;
    if (bOldGuard || bHighlander)
    {
        Add(P::Frustum, M::Dark, {-2, 0, bOldGuard ? 178.0 : 167.0},
            {39, 42, bOldGuard ? 54.0 : 34.0});
        Add(P::Ellipsoid, M::Dark, {-3, 0, bOldGuard ? 200.0 : 181.0}, {39, 41, 19});
        Add(P::BeveledBlock, bOldGuard ? M::Bronze : M::TartanStripe, {18, 0, bOldGuard ? 172.0 : 155.0},
            {6, 25, bOldGuard ? 20.0 : 7.0});
        Add(P::Ellipsoid, bOldGuard ? M::Scarlet : M::Ivory, {-3, -21, bOldGuard ? 207.0 : 182.0},
            {10, 11, bOldGuard ? 30.0 : 22.0}, FRotator(-14, 0, 0));
    }
    else
    {
        Add(P::Frustum, M::Dark, {0, 0, bTallShako ? 170.0 : 164.0}, {37, 37, bTallShako ? 33.0 : 25.0});
        Add(P::Disc, M::Dark, {14, 0, 153}, {31, 37, 5}, FRotator(-8, 0, 0));
        Add(P::BeveledBlock, M::Bronze, {16, 0, 166}, {6, 13, 12});
        const M Plume = bLight ? M::Green : Style == S::BritishGrenadier ? M::Ivory
            : bGrenadier || Style == S::YoungGuard ? M::Scarlet : bBritish ? M::Ivory : M::Ochre;
        const double PlumeHeight = Style == S::RussianGrenadier ? 32.0 : bGrenadier ? 22.0 : bLight ? 16.0 : 10.0;
        Add(P::Ellipsoid, Plume, {6, -10, (bTallShako ? 185.0 : 176.0) + PlumeHeight * .35},
            {11, 11, PlumeHeight});
        if (Style == S::Chasseur)
            Add(P::Ellipsoid, M::Ochre, {7, -10, 189}, {8, 9, 8});
        if (Style == S::BritishLine)
            Add(P::Ellipsoid, M::Scarlet, {6, -10, 177}, {11, 11, 7});
    }
    Add(P::BeveledBlock, Coat, {0, 0, 107}, {42, 54, 48});
    Add(P::BeveledBlock, bBritish ? M::Dark : M::Scarlet, {5, 0, 130}, {31, 40, 9});
    if (!bBritish && !bRussian)
        Add(P::BeveledBlock, bLight ? M::Navy : M::Ivory, {22, 0, 113}, {5, 24, 30});
    const M Straps = bLight && bRussian ? M::Dark : M::Linen;
    Bar(Model, Straps, {25, -18, 127}, {25, 19, 90}, 6);
    if (!bRussian)
        Bar(Model, Straps, {25, 18, 127}, {25, -19, 90}, 6);
    if (bGrenadier || bOldGuard || bLight)
        for (double Y : {-25., 25.})
            Add(P::BeveledBlock, bLight ? M::Green : M::Scarlet, {1, Y, 126}, {30, 13, 7});
    if (bRussian)
    {
        Bar(Model, M::GreyWool, {28, -25, 134}, {26, 24, 88}, 17, P::Disc, F::Greatcoat);
        Add(P::Disc, M::GreyWool, {-1, -23, 133}, {19, 20, 25}, FRotator(71, 0, 0));
        Bar(Model, M::GreyWool, {-22, -24, 131}, {-23, 22, 89}, 15, P::Disc);
    }
    if (bHighlander)
    {
        Add(P::Frustum, M::Tartan, {0, 0, 72}, {45, 61, 39}, FRotator(180, 0, 0), F::Kilt);
        for (double Z : {59., 72., 85.})
            Add(P::Disc, M::TartanStripe, {0, 0, Z},
                {42 + (72 - Z) * .27, 57 + (72 - Z) * .4, 4}, FRotator::ZeroRotator, F::Kilt);
        for (double Y : {-14., 0., 14.})
            Add(P::BeveledBlock, M::TartanStripe, {19, Y, 71}, {5, 3, 33}, FRotator(-9, 0, 0), F::Kilt);
        Add(P::BeveledBlock, M::Linen, {23, 0, 74}, {13, 19, 24});
    }
    return Model;
}

FRegimentAncientFigure BuildRegimentHistoricalTack(ERegimentHistoricalFigure Style)
{
    using S = ERegimentHistoricalFigure;
    FRegimentAncientFigure Model;
    if (Style == S::Numidian || Style == S::RomanCavalry || Style == S::Companion)
        Part(Model, P::Disc, M::Leather, {41, 0, 132}, {28, 37, 6}, FRotator(-25, 0, 0));
    if (Style == S::RomanCavalry || Style == S::Companion)
    {
        Part(Model, P::BeveledBlock, M::Paint, {-5, 0, 113}, {55, 56, 17});
        for (double Y : {-23., 23.})
            Part(Model, P::BeveledBlock, M::Leather, {-3, Y, 111}, {43, 12, 19});
    }
    if (Style == S::SengokuCavalry)
    {
        Part(Model, P::BeveledBlock, M::Paint, {-5, 0, 112}, {71, 58, 17});
        for (double X : {-24., 21.})
            Part(Model, P::BeveledBlock, M::Dark, {X, 0, 120}, {10, 47, 19}, FRotator(X > 0 ? -13 : 13, 0, 0));
        for (double Side : {-1., 1.})
        {
            Bar(Model, M::Leather, {1, Side * 24, 111}, {5, Side * 29, 74}, 4);
            Part(Model, P::BeveledBlock, M::Iron, {11, Side * 29, 72}, {24, 10, 5}, FRotator(7, 0, 0));
        }
    }
    if (IsRefreshedFigure(Style) && IsHistoricalRider(Style))
    {
        const bool bPeriodStirrups = Style >= S::WesternMail;
        Part(Model, P::BeveledBlock, M::Paint, {-12, 0, 106}, {80, 55, 16});
        Part(Model, P::BeveledBlock, M::Leather, {-4, 0, 115}, {53, 39, 12});
        for (double Side : {-1., 1.})
        {
            Bar(Model, M::Leather, {56, Side * 12, 127}, {4, Side * 21, 123}, 3.5);
            Part(Model, P::BeveledBlock, M::Leather, {-18, Side * 21, 102}, {39, 12, 24});
            if (bPeriodStirrups)
            {
                Bar(Model, M::Leather, {4, Side * 24, 108}, {9, Side * 27, 78}, 4);
                Part(Model, P::BeveledBlock, M::Iron, {11, Side * 27, 77}, {14, 9, 5});
            }
            if (Style == S::Cataphract)
                for (double Z : {84., 96., 108.})
                    Part(Model, P::BeveledBlock, M::Iron, {-9, Side * 25, Z}, {92, 9, 12});
        }
        if (Style == S::Cataphract)
            Part(Model, P::BeveledBlock, M::Iron, {67, 0, 137}, {24, 22, 38}, FRotator(-23, 0, 0));
    }
    return Model;
}

FRegimentAncientFigure BuildRegimentHistoricalProxy(ERegimentHistoricalFigure Style)
{
    using S = ERegimentHistoricalFigure;
    const bool bSengoku = IsSengokuFigure(Style);
    const bool bRefreshed = IsRefreshedFigure(Style);
    auto Model = bSengoku ? SengokuDress(Style, true) :
        bRefreshed ? RefreshedDress(Style, true) : BuildRegimentHistoricalDress(Style);
    if (Model.Parts.IsEmpty())
        return Model;
    Model.bSimplified = true;
    const bool bMounted = IsHistoricalRider(Style);
    const double RideHeight = bMounted ? RegimentHistoricalRiderHeight : 0.0;
    if (bMounted)
    {
        const FVector RiderOffset(0, 0, RideHeight);
        for (auto& Piece : Model.Parts)
            Piece.Transform.AddToTranslation(RiderOffset);
        Model.Bounds = Model.Bounds.ShiftBy(RiderOffset);
        auto Mount = BuildRegimentHistoricalTack(Style);
        FRegimentAncientFigure HorseBody;
        Horse(HorseBody, FVector::ZeroVector, Style == S::Numidian ? M::Leather : M::Hide, 6, false);
        for (const auto& Piece : HorseBody.Parts)
            Part(Mount, Piece.Primitive, Piece.Material, Piece.Transform.GetLocation() * FVector(.72, 1, 1),
                Piece.Transform.GetScale3D() * FVector(.72, 1, 1), Piece.Transform.Rotator(), Piece.Feature);
        Model.Bounds += Mount.Bounds;
        Model.Parts.Append(MoveTemp(Mount.Parts));
    }
    const bool bKilt = Style == S::Highlander;
    const M Coat = bSengoku || bRefreshed ? M::Paint : Style >= S::BritishLine ? M::Scarlet : Style >= S::RussianLine ? M::Green :
        Style >= S::Fusilier ? M::Navy : Style == S::Numidian ? M::Linen : M::Paint;
    const M Trousers = bSengoku || bRefreshed ? M::Dark : bKilt ? M::Skin : Style >= S::Fusilier ? M::Linen : M::Paint;
    Part(Model, P::BeveledBlock, Coat, {0, 0, 89 + RideHeight}, {32, 43, 64});
    Part(Model, P::BeveledBlock, M::Skin, {2, 0, 144 + RideHeight}, {27, 26, 27});
    for (double Side : {-1., 1.})
    {
        Part(Model, P::BeveledBlock, Coat, {9, Side * 26, 110 + RideHeight}, {22, 14, 35}, FRotator(-22, 0, 0));
        Part(Model, P::BeveledBlock, M::Skin, {19, Side * 29, 93 + RideHeight}, {13, 13, 17});
        Part(Model, P::BeveledBlock, Trousers, {bMounted ? 12.0 : 0.0, Side * (bMounted ? 23.0 : 12.0), 38 + RideHeight},
            {18, 17, bMounted ? 48.0 : 55.0}, FRotator(bMounted ? -22.0 : 0.0, 0, 0));
        Part(Model, P::BeveledBlock, M::Dark,
            {bMounted ? 13.0 : 3.0, Side * (bMounted ? 25.0 : 12.0), (bMounted ? 25.0 : 15.0) + RideHeight},
            {22, 17, bKilt ? 30.0 : 27.0});
    }
    return Model;
}

FRegimentAncientFigure BuildRegimentSengokuWeapon(ERegimentHistoricalFigure Style)
{
    using S = ERegimentHistoricalFigure;
    FRegimentAncientFigure Model;
    if (Style == S::SengokuArcher)
    {
        // The asymmetric limbs meet at the hand, about one third of the way up the bow.
        const FVector Limbs[] = {{-18, 0, -57}, {-5, 0, -41}, {0, 0, -17}, {0, 0, 17},
            {5, 0, 58}, {0, 0, 96}, {-23, 0, 123}};
        for (int32 Segment = 1; Segment < UE_ARRAY_COUNT(Limbs); ++Segment)
            Bar(Model, M::Wood, Limbs[Segment - 1], Limbs[Segment], Segment == 3 ? 4.5 : 3.2);
        Bar(Model, M::Linen, Limbs[0], Limbs[UE_ARRAY_COUNT(Limbs) - 1], 1.35);
        Bar(Model, M::Dark, {0, 0, -8}, {0, 0, 8}, 5.3, P::Disc);
    }
    else if (Style == S::SengokuArquebusier)
    {
        Bar(Model, M::Iron, {0, 0, 4}, {94, 0, 4}, 5.5, P::Disc);
        Bar(Model, M::Wood, {-12, 0, -1}, {72, 0, -1}, 7.0, P::BeveledBlock);
        Bar(Model, M::Wood, {-35, 0, -11}, {-12, 0, -1}, 9, P::BeveledBlock);
        Part(Model, P::BeveledBlock, M::Bronze, {8, 4, 4}, {14, 3, 7}, FRotator::ZeroRotator, F::Matchlock);
        Part(Model, P::Disc, M::Dark, {92, 0, 4}, {7, 7, 7}, FRotator(90, 0, 0));
    }
    else if (Style == S::SengokuSamurai)
    {
        Bar(Model, M::Iron, {0, 0, 12}, {4, 0, 47}, 3, P::BeveledBlock);
        Bar(Model, M::Iron, {4, 0, 47}, {14, 0, 81}, 3, P::BeveledBlock);
        Bar(Model, M::Iron, {14, 0, 81}, {18, 0, 90}, 3, P::Taper);
        Bar(Model, M::Dark, {0, 0, -15}, {0, 0, 12}, 5, P::Disc);
        Part(Model, P::Disc, M::Bronze, {0, 0, 12}, {11, 10, 3});
    }
    return Model;
}

FRegimentAncientFigure BuildRegimentSengokuBanner()
{
    FRegimentAncientFigure Model;
    Model.bSimplified = true;
    Bar(Model, M::Wood, {-24, 10, 92}, {-34, 10, 231}, 3.5);
    Bar(Model, M::Wood, {-34, 9, 231}, {-34, 44, 231}, 3.0);
    Part(Model, P::BeveledBlock, M::Paint, {-35, 28, 207}, {4, 31, 45}, FRotator(-4, 0, 0), F::Sashimono);
    for (double X : {-38., -32.})
        Part(Model, P::Disc, M::Linen, {X, 28, 212}, {15, 15, 2.6}, FRotator(90, 0, 0));
    return Model;
}

FRegimentAncientFigure BuildRegimentLooseWeapon(battle::RangedWeapon Weapon)
{
    using W = battle::RangedWeapon;
    FRegimentAncientFigure Model;
    if (Weapon == W::Javelin)
    {
        Bar(Model, M::Wood, {0, 0, -53}, {0, 0, 76}, 5, P::Disc);
        Bar(Model, M::Iron, {0, 0, 74}, {0, 0, 89}, 8, P::Taper);
    }
    else if (Weapon == W::Sling)
    {
        Bar(Model, M::Linen, {0, 0, 0}, {0, -9, 30}, 3);
        Bar(Model, M::Linen, {0, 0, 0}, {2, 12, 27}, 3);
        Bar(Model, M::Leather, {0, -9, 30}, {2, 12, 27}, 8, P::BeveledBlock, F::SlingPouch);
    }
    else if (Weapon == W::Bow)
    {
        for (double Side : {-1., 1.})
        {
            const FVector End(-17, 0, Side * 48);
            Bar(Model, M::Wood, FVector::ZeroVector, {0, 0, Side * 25}, 6);
            Bar(Model, M::Wood, {0, 0, Side * 25}, {-7, 0, Side * 39}, 5);
            Bar(Model, M::Wood, {-7, 0, Side * 39}, End, 4);
            Bar(Model, M::Linen, End, {-16, 0, 0}, 2);
        }
    }
    return Model;
}

double RegimentEraFigureScale(ERegimentEraVisual Style)
{
    switch (Style)
    {
    case ERegimentEraVisual::Giant: return 1.62;
    case ERegimentEraVisual::Dwarven: return .81;
    case ERegimentEraVisual::Orcish: return 1.10;
    default: return 1.0;
    }
}

FRegimentAncientFigure BuildRegimentEraHeadgear(ERegimentEraVisual Style)
{
    FRegimentAncientFigure Figure;
    const auto Piece = [&](P Shape, M Surface, FVector At, FVector Size, FRotator Rotation = FRotator::ZeroRotator)
    {
        Part(Figure, Shape, Surface, At, Size, Rotation);
    };
    switch (Style)
    {
    case ERegimentEraVisual::Plate:
        Piece(P::Ellipsoid, M::Iron, {0, 0, 8}, {36, 35, 34});
        Piece(P::BeveledBlock, M::Dark, {16, 0, 4}, {7, 29, 7});
        Piece(P::BeveledBlock, M::Paint, {-5, 0, 25}, {31, 8, 12}, FRotator(17, 0, 0));
        break;
    case ERegimentEraVisual::PikeShot:
        Piece(P::Disc, M::Iron, {0, 0, 5}, {53, 39, 7}, FRotator(-14, 0, 0));
        Piece(P::Ellipsoid, M::Iron, {0, 0, 14}, {35, 33, 29});
        Piece(P::BeveledBlock, M::Bronze, {0, 0, 27}, {33, 6, 9});
        break;
    case ERegimentEraVisual::Eastern:
        Piece(P::Disc, M::Paint, {0, 0, 5}, {40, 39, 13});
        Piece(P::Taper, M::Iron, {0, 0, 22}, {35, 35, 32});
        Piece(P::Ellipsoid, M::Bronze, {0, 0, 40}, {7, 7, 9});
        break;
    case ERegimentEraVisual::Sengoku:
        Piece(P::Ellipsoid, M::Dark, {0, 0, 13}, {39, 38, 32});
        Piece(P::Disc, M::Iron, {-1, 0, 4}, {46, 50, 5});
        for (double Side : {-1.0, 1.0})
        {
            Piece(P::BeveledBlock, M::Paint, {-6, Side * 20, -4}, {28, 10, 20}, FRotator(0, 0, Side * 18));
            Piece(P::Taper, M::Bronze, {13, Side * 11, 24}, {8, 8, 24}, FRotator(-15, 0, Side * 24));
        }
        break;
    case ERegimentEraVisual::Tricorn:
        Piece(P::Disc, M::Dark, {0, 0, 8}, {40, 40, 21});
        for (double Angle : {0.0, 120.0, 240.0})
        {
            const FRotator Turn(0, Angle, 0);
            Piece(P::BeveledBlock, M::Dark, Turn.RotateVector({15, 0, 9}), {16, 34, 15}, Turn);
            Piece(P::BeveledBlock, M::Bronze, Turn.RotateVector({21, 0, 16}), {4, 31, 4}, Turn);
        }
        break;
    case ERegimentEraVisual::Shako:
        Piece(P::Disc, M::Dark, {0, 0, 24}, {33, 33, 42});
        Piece(P::Disc, M::Dark, {14, 0, 4}, {33, 35, 5});
        Piece(P::Disc, M::Bronze, {0, 0, 39}, {34, 34, 4});
        Piece(P::Ellipsoid, M::Paint, {1, 0, 53}, {11, 11, 19});
        break;
    case ERegimentEraVisual::Elven:
        Piece(P::Taper, M::Iron, {-4, 0, 20}, {32, 31, 42}, FRotator(-16, 0, 0));
        for (double Side : {-1.0, 1.0})
            Piece(P::Taper, M::Paint, {-5, Side * 17, 16}, {10, 8, 30}, FRotator(-22, 0, Side * 21));
        break;
    case ERegimentEraVisual::Orcish:
        Piece(P::BeveledBlock, M::Iron, {-1, 0, 7}, {38, 43, 24});
        for (double Side : {-1.0, 1.0})
            Piece(P::Taper, M::Ivory, {1, Side * 26, 18}, {13, 13, 30}, FRotator(-12, 0, Side * 32));
        break;
    case ERegimentEraVisual::Undead:
        Piece(P::BeveledBlock, M::Ivory, {2, 0, 4}, {31, 31, 28});
        for (double Side : {-1.0, 1.0})
            Piece(P::Block, M::Dark, {18, Side * 8, 5}, {3, 8, 7});
        Piece(P::Disc, M::Iron, {-1, 0, 17}, {32, 34, 8});
        break;
    case ERegimentEraVisual::Dwarven:
        Piece(P::BeveledBlock, M::Bronze, {0, 0, 8}, {43, 43, 27});
        Piece(P::BeveledBlock, M::Paint, {16, 0, -16}, {15, 31, 26});
        Piece(P::BeveledBlock, M::Iron, {0, 0, 25}, {39, 12, 10});
        break;
    case ERegimentEraVisual::Giant:
        Piece(P::BeveledBlock, M::Hide, {-2, 0, 6}, {44, 42, 24});
        Piece(P::Block, M::Bronze, {10, 0, 4}, {27, 38, 6});
        Piece(P::Ellipsoid, M::Iron, {15, 0, 13}, {14, 12, 17});
        break;
    case ERegimentEraVisual::Arcane:
        Piece(P::Disc, M::Paint, {-3, 0, 7}, {55, 55, 7});
        Piece(P::Taper, M::Paint, {-5, 0, 33}, {38, 37, 50}, FRotator(-14, 0, 0));
        Piece(P::Disc, M::Bronze, {-2, 0, 14}, {36, 36, 5});
        break;
    default:
        break;
    }
    return Figure;
}

UStaticMesh* MakeRegimentAncientMesh(UObject& Owner, const FRegimentAncientFigure& Figure,
    UMaterialInterface* MaterialParent, int32& PaintSlot, int32* HideSlot)
{
    PaintSlot = INDEX_NONE;
    if (HideSlot)
        *HideSlot = INDEX_NONE;
    if (Figure.Parts.IsEmpty())
        return nullptr;
    auto* Mesh = NewObject<UStaticMesh>(&Owner, NAME_None, RF_Transient);
    FMeshDescription Description;
    FStaticMeshAttributes Attributes(Description);
    Attributes.Register();
    auto Positions = Attributes.GetVertexPositions();
    auto Normals = Attributes.GetVertexInstanceNormals();
    auto Tangents = Attributes.GetVertexInstanceTangents();
    auto Signs = Attributes.GetVertexInstanceBinormalSigns();
    Attributes.GetVertexInstanceUVs().SetNumChannels(1);
    auto SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
    TMap<M, FPolygonGroupID> Groups;
    TMap<P, FAncientPrimitiveMesh> Geometry;
    const FColor Colors[] = {FColor(115, 79, 139), FColor(185, 129, 65), FColor(121, 77, 43),
        FColor(86, 48, 37), FColor(238, 220, 174), FColor(103, 111, 121), FColor(41, 39, 40),
        FColor(192, 137, 97), FColor(159, 169, 178), FColor(209, 201, 176), FColor(176, 42, 37), FColor(42, 91, 55),
        FColor(37, 51, 95), FColor(44, 77, 78), FColor(119, 53, 42), FColor(189, 121, 56),
        FColor(109, 106, 98), FColor(177, 130, 72)};
    static_assert(UE_ARRAY_COUNT(Colors) == int32(M::Count));
    for (const auto& Part : Figure.Parts)
    {
        if (!Groups.Contains(Part.Material))
        {
            const auto Group = Description.CreatePolygonGroup();
            const FName Name = FName(*FString::Printf(TEXT("Ancient_%d"), int32(Part.Material)));
            SlotNames[Group] = Name;
            auto* Material = MaterialParent ? UMaterialInstanceDynamic::Create(MaterialParent, Mesh) : nullptr;
            if (Material)
            {
                Material->SetVectorParameterValue(TEXT("Color"), FLinearColor::FromSRGBColor(Colors[int32(Part.Material)]));
                Material->SetScalarParameterValue(TEXT("Roughness"), .88f);
            }
            const int32 Index = Mesh->GetStaticMaterials().Emplace(Material, Name);
            if (Part.Material == M::Paint)
                PaintSlot = Index;
            if (HideSlot && Part.Material == M::Hide)
                *HideSlot = Index;
            Groups.Add(Part.Material, Group);
        }
        if (!Geometry.Contains(Part.Primitive))
            Geometry.Add(Part.Primitive, Primitive(Part.Primitive, Figure.bSimplified));
        const auto& PrimitiveGeometry = Geometry.FindChecked(Part.Primitive);
        TArray<FVertexID, TInlineAllocator<64>> Vertices;
        for (const FVector& Point : PrimitiveGeometry.Points)
        {
            const auto Vertex = Description.CreateVertex();
            Positions[Vertex] = FVector3f(Part.Transform.TransformPosition(Point));
            Vertices.Add(Vertex);
        }
        const FPolygonGroupID Group = Groups.FindChecked(Part.Material);
        for (const FIntVector& Face : PrimitiveGeometry.Faces)
        {
            const FVector A(Positions[Vertices[Face.X]]);
            const FVector B(Positions[Vertices[Face.Y]]);
            const FVector C(Positions[Vertices[Face.Z]]);
            FVector Normal = FVector::CrossProduct(B - A, C - A).GetSafeNormal();
            const bool bOutward = FVector::DotProduct(Normal, (A + B + C) / 3 - Part.Transform.GetLocation()) > 0;
            if (!bOutward)
                Normal = -Normal;
            const FVector3f Tangent((B - A).GetSafeNormal());
            const FVertexInstanceID Corners[] = {
                Description.CreateVertexInstance(Vertices[Face.X]),
                Description.CreateVertexInstance(Vertices[bOutward ? Face.Z : Face.Y]),
                Description.CreateVertexInstance(Vertices[bOutward ? Face.Y : Face.Z])
            };
            for (const auto Corner : Corners)
            {
                Normals[Corner] = FVector3f(Normal);
                Tangents[Corner] = Tangent;
                Signs[Corner] = 1.f;
            }
            Description.CreateTriangle(Group, Corners);
        }
    }
    UStaticMesh::FBuildMeshDescriptionsParams Params;
    Params.bFastBuild = true;
    Params.bCommitMeshDescription = false;
    Params.bMarkPackageDirty = false;
    if (!Mesh->BuildFromMeshDescriptions({&Description}, Params))
    {
        PaintSlot = INDEX_NONE;
        return nullptr;
    }
    return Mesh;
}
