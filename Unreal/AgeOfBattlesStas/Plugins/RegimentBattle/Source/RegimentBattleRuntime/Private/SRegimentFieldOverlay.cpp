#include "SRegimentFieldOverlay.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

#define LOCTEXT_NAMESPACE "RegimentFieldOverlay"

namespace
{
const FSlateRoundedBoxBrush Flag(FLinearColor(.018f, .024f, .022f, .92f), 4.f,
    FLinearColor(.35f, .38f, .34f), 1.f);

FLinearColor LineColor(ERegimentFieldLineKind Kind)
{
    switch (Kind)
    {
    case ERegimentFieldLineKind::Blocked: return FLinearColor(FColor::FromHex(TEXT("FF9579")));
    case ERegimentFieldLineKind::Front: return FLinearColor(FColor::FromHex(TEXT("EFC469")));
    case ERegimentFieldLineKind::Fire: return FLinearColor(FColor::FromHex(TEXT("F6D698")));
    case ERegimentFieldLineKind::FireFriendly: return FLinearColor(FColor::FromHex(TEXT("52B6FF")));
    case ERegimentFieldLineKind::FireMelee: return FLinearColor(FColor::FromHex(TEXT("D493FF")));
    case ERegimentFieldLineKind::FireRisk: return FLinearColor(FColor::FromHex(TEXT("FFAA59")));
    case ERegimentFieldLineKind::FireBlocked: return FLinearColor(FColor::FromHex(TEXT("CA7771")));
    default: return FLinearColor(FColor::FromHex(TEXT("91DFC4")));
    }
}
}

void SRegimentFieldOverlay::Construct(const FArguments& Args)
{
    Owner = Args._Owner;
    Lines = Args._Lines;
    Cards = Args._Cards;
    SetVisibility(EVisibility::HitTestInvisible);
    ForceVolatile(true);
}

int32 SRegimentFieldOverlay::OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
    FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle&, bool) const
{
    const auto* World = Owner.IsValid() ? Owner->GetWorld() : nullptr;
    const auto* Controller = World ? World->GetFirstPlayerController() : nullptr;
    if (!Controller)
        return Layer;
    const auto Segments = Lines ? Lines() : TConstArrayView<FRegimentFieldLine>();
    const auto Units = Cards ? Cards() : TConstArrayView<FRegimentFieldCard>();
    if (Segments.IsEmpty() && Units.IsEmpty())
        return Layer;
    int32 Width = 0, Height = 0;
    Controller->GetViewportSize(Width, Height);
    if (Width <= 0 || Height <= 0)
        return Layer;
    const FVector2D Scale = Geometry.GetLocalSize() / FVector2D(Width, Height);

    for (const auto& Segment : Segments)
    {
        FVector2D From, To;
        if (!Controller->ProjectWorldLocationToScreen(Segment.From, From, true) ||
            !Controller->ProjectWorldLocationToScreen(Segment.To, To, true))
            continue;
        From *= Scale;
        To *= Scale;
        // A route can start on its first waypoint. Slate's antialiased batch needs a visible segment.
        if (From.ContainsNaN() || To.ContainsNaN() || From.Equals(To, .05))
            continue;
        const TArray<FVector2D> Points{From, To};
        FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Points,
            ESlateDrawEffect::None, FLinearColor(0.02f, 0.02f, 0.01f, .8f), true, 4.f);
        FSlateDrawElement::MakeLines(Elements, Layer + 1, Geometry.ToPaintGeometry(), Points,
            ESlateDrawEffect::None, LineColor(Segment.Kind), true, 2.f);
    }
    const FVector2D Size = Geometry.GetLocalSize();
    const auto Font = FCoreStyle::GetDefaultFontStyle("Bold", 13);
    const auto Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    TArray<FBox2D, TInlineAllocator<12>> Used;
    for (const auto& Card : Units)
    {
        if (Card.Living <= 0)
            continue;
        FVector2D Point;
        if (!Controller->ProjectWorldLocationToScreen(Card.Location, Point, true))
            continue;
        Point *= Scale;
        if (Point.X < 60 || Point.X > Size.X - 60 || Point.Y < 106 || Point.Y > Size.Y - 190)
            continue;
        const FText Symbol = FText::Format(LOCTEXT("FlagRegiment", "{0} · {1}"), Card.Id,
            !Card.FlagState.IsEmpty() ? Card.FlagState : Card.bDisordered ? LOCTEXT("FlagBroken", "Строй нарушен") :
            Card.bContact ? LOCTEXT("FlagContact", "Контакт") : LOCTEXT("FlagReady", "В строю"));
        const FVector2D TextSize = Measure->Measure(Symbol, Font);
        const FVector2D FlagSize = TextSize + FVector2D(16, 8);
        const FVector2D At = Point - FVector2D(FlagSize.X * .5, FlagSize.Y + 12);
        const FBox2D Bounds(At - FVector2D(3, 3), At + FlagSize + FVector2D(3, 3));
        if (Used.ContainsByPredicate([&Bounds](const auto& Other) { return Other.Intersect(Bounds); }))
            continue;
        Used.Add(Bounds);
        const FLinearColor Tint = Card.bDisordered ? FLinearColor(FColor::FromHex(TEXT("FFAF92"))) :
            Card.bContact ? FLinearColor(FColor::FromHex(TEXT("F4D49A"))) :
            FLinearColor(FColor::FromHex(TEXT("84CDFA")));
        FSlateDrawElement::MakeLines(Elements, Layer + 2, Geometry.ToPaintGeometry(),
            TArray<FVector2D>{At + FVector2D(4, FlagSize.Y), At + FVector2D(4, FlagSize.Y + 12)},
            ESlateDrawEffect::None, Tint, true, 1.f);
        FSlateDrawElement::MakeBox(Elements, Layer + 2, Geometry.ToPaintGeometry(FlagSize, FSlateLayoutTransform(At)),
            &Flag, ESlateDrawEffect::None, Flag.GetTint(FWidgetStyle()));
        FSlateDrawElement::MakeText(Elements, Layer + 3,
            Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(At + FVector2D(8, 4))),
            Symbol, Font, ESlateDrawEffect::None, Tint);
    }
    return Layer + 3;
}

#undef LOCTEXT_NAMESPACE
