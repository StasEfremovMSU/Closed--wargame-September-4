#include "SRegimentMissionDeploymentMap.h"

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SCompoundWidget.h"

namespace
{
const FLinearColor Blue(0.20f, 0.68f, 1.0f);
const FLinearColor Orange(1.f, 0.51f, 0.20f);
const FLinearColor Gold(1.f, 0.79f, 0.29f);
const FLinearColor Moved(0.76f, 0.74f, 0.71f);

FLinearColor TerrainColor(battle::TerrainFeature Feature)
{
    using enum battle::TerrainFeature;
    switch (Feature)
    {
        case River: return {0.12f, 0.30f, 0.47f};
        case Bridge:
        case Ford: return {0.38f, 0.55f, 0.62f};
        case Road: return {0.47f, 0.42f, 0.30f};
        case PloughedField: return {0.36f, 0.27f, 0.17f};
        case Forest: return {0.11f, 0.28f, 0.19f};
        case Marsh: return {0.18f, 0.31f, 0.29f};
        case RedoubtRamp: return {0.34f, 0.23f, 0.15f};
        case RedoubtBank: return {0.52f, 0.36f, 0.21f};
        case RedoubtInterior: return {0.44f, 0.31f, 0.18f};
        default: return {0.49f, 0.48f, 0.46f};
    }
}

class SMissionDeploymentMap : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMissionDeploymentMap) {}
        SLATE_ARGUMENT(const TOptional<FMissionAuthoringPreview>*, Preview)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Preview = Args._Preview;
        SetClipping(EWidgetClipping::ClipToBounds);
    }

    int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle&, bool) const override
    {
        if (!Preview || !Preview->IsSet())
            return Layer;
        const auto& Plan = Preview->GetValue();
        const FVector2D Area = Geometry.GetLocalSize();
        const FVector2D FieldSize(Plan.Field.width_m, Plan.Field.height_m);
        const double Meter = FMath::Min((Area.X - 24) / FieldSize.X, (Area.Y - 24) / FieldSize.Y);
        if (Meter <= 0)
            return Layer;

        const auto Project = [&Area, Meter](double X, double Y)
        {
            return Area * .5 + FVector2D(X, -Y) * Meter;
        };
        const auto Box = [&](FVector2D Point, FVector2D Size, FLinearColor Color, int32 Offset = 0)
        {
            FSlateDrawElement::MakeBox(Elements, Layer + Offset,
                Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Point)),
                FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Color);
        };
        const auto Line = [&](const TArray<FVector2D>& Points, FLinearColor Color, int32 Offset, float Thickness)
        {
            FSlateDrawElement::MakeLines(Elements, Layer + Offset, Geometry.ToPaintGeometry(),
                Points, ESlateDrawEffect::None, Color, true, Thickness);
        };
        const auto Ellipse = [&](battle::Vec3 Center, float Rx, float Ry)
        {
            TArray<FVector2D> Curve;
            for (int32 Index = 0; Index <= 64; ++Index)
            {
                const double Angle = UE_TWO_PI * Index / 64.0;
                Curve.Add(Project(Center.x + Rx * FMath::Cos(Angle), Center.y + Ry * FMath::Sin(Angle)));
            }
            Line(Curve, Gold, 3, 2.f);
        };
        const double HalfX = FieldSize.X * .5, HalfY = FieldSize.Y * .5;
        const FLinearColor Meadow(.20f, .26f, .19f);
        Box(Project(-HalfX, HalfY), FieldSize * Meter, Meadow);
        if (Plan.Hill)
        {
            // Sample the same surface elevation used by uphill movement and crest control.
            constexpr int32 Columns = 28, Rows = 20;
            const FVector2D Step(FieldSize.X / Columns, FieldSize.Y / Rows);
            for (int32 Row = 0; Row < Rows; ++Row)
                for (int32 Column = 0; Column < Columns; ++Column)
                {
                    const double X = -HalfX + Step.X * Column;
                    const double Y = HalfY - Step.Y * Row;
                    const float Height = battle::HillHeight(*Plan.Hill,
                        {float(X + Step.X * .5), float(Y - Step.Y * .5), 0.f});
                    const float Ratio = FMath::Clamp(Height / Plan.Hill->height_m, 0.f, 1.f);
                    Box(Project(X, Y), Step * Meter, FMath::Lerp(Meadow, FLinearColor(.55f, .52f, .34f), Ratio), 1);
                }
        }
        for (const auto& Feature : Plan.Terrain)
            Box(Project(Feature.min_x, Feature.max_y),
                FVector2D(Feature.max_x - Feature.min_x, Feature.max_y - Feature.min_y) * Meter,
                TerrainColor(Feature.feature), 2);
        if (Plan.Crest)
            Ellipse(Plan.Crest->center, Plan.Crest->radius_x_m, Plan.Crest->radius_y_m);
        else if (Plan.Objective)
            Ellipse(Plan.Objective->center, Plan.Objective->radius_m, Plan.Objective->radius_m);

        const auto Footprint = [&](const battle::Pose& Pose, battle::FormationFootprint Size)
        {
            const auto Front = battle::Forward(Pose.yaw) * (Size.depth_m * .5f);
            const battle::Vec3 Left{-FMath::Sin(Pose.yaw) * (Size.width_m * .5f),
                FMath::Cos(Pose.yaw) * (Size.width_m * .5f), 0.f};
            const auto A = Pose.position + Front + Left, B = Pose.position + Front - Left;
            const auto C = Pose.position - Front - Left, D = Pose.position - Front + Left;
            return TArray<FVector2D>{Project(A.x, A.y), Project(B.x, B.y), Project(C.x, C.y),
                Project(D.x, D.y), Project(A.x, A.y)};
        };
        for (const auto& Spawn : Plan.Spawns)
        {
            const bool bBlue = Spawn.Slot < MissionRegimentsPerTeam;
            const FLinearColor Color = bBlue ? Blue : Orange;
            const auto& From = Spawn.Requested.position;
            const auto& To = Spawn.Actual.position;
            const auto Pixel = Project(To.x, To.y);
            if (From != To || Spawn.Requested.yaw != Spawn.Actual.yaw)
            {
                Line(Footprint(Spawn.Requested, Spawn.Size), Moved, 4, 1.f);
                Line({Project(From.x, From.y), Pixel}, Moved, 4, 1.f);
            }
            Line(Footprint(Spawn.Actual, Spawn.Size), Color, 5, 2.f);
            const auto Forward = Spawn.Actual.position + battle::Forward(Spawn.Actual.yaw) * (Spawn.Size.depth_m * .5f + 3.f);
            Line({Pixel, Project(Forward.x, Forward.y)}, Color, 5, 1.4f);
            const FString Number = FString::Printf(TEXT("%d"), Spawn.Slot % MissionRegimentsPerTeam + 1);
            FSlateDrawElement::MakeText(Elements, Layer + 6,
                Geometry.ToPaintGeometry(FVector2D(20, 16), FSlateLayoutTransform(Pixel + FVector2D(-4, -9))),
                Number, FCoreStyle::GetDefaultFontStyle("Bold", 10), ESlateDrawEffect::None, FLinearColor::White);
        }
        return Layer + 6;
    }

private:
    const TOptional<FMissionAuthoringPreview>* Preview = nullptr;
};

}

TSharedRef<SWidget> MakeMissionDeploymentMap(const TOptional<FMissionAuthoringPreview>* Preview)
{
    return SNew(SMissionDeploymentMap).Preview(Preview);
}
