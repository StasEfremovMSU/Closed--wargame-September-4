#include "SRegimentBattleHud.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/World.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "RegimentAudioSettings.h"
#include "RegimentBattleField.h"
#include "RegimentBattleNotices.h"
#include "RegimentBattlePlayback.h"
#include "RegimentOfficerSettings.h"
#include "SRegimentOfficerPicker.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "SRegimentCommandPanel.h"
#include "SRegimentCommanderAvatar.h"
#include "SRegimentFieldOverlay.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SInputKeySelector.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RegimentBattleHud"

namespace
{
using RegimentAudioSettings::EMenuSound;
using RegimentAudioSettings::PlayMenuSound;

const FLinearColor Ink = FLinearColor(FColor::FromHex(TEXT("F7F3EB")));
const FLinearColor Muted = FLinearColor(FColor::FromHex(TEXT("BDC8D0")));
const FLinearColor Brass = FLinearColor(FColor::FromHex(TEXT("F0C98E")));
const FLinearColor ControlTint = FLinearColor(FColor::FromHex(TEXT("353A2E")));
const FLinearColor SelectedTint = FLinearColor(FColor::FromHex(TEXT("7A5527")));
const FLinearColor RosterAccent = FLinearColor(FColor::FromHex(TEXT("CEDBA6")));
const FLinearColor RosterInk = FLinearColor(FColor::FromHex(TEXT("182018")));
const FLinearColor OfficerControlTint = FLinearColor(FColor::FromHex(TEXT("42352A")));
const FLinearColor Blue = FLinearColor(FColor::FromHex(TEXT("84CDFA")));
const FLinearColor Orange = FLinearColor(FColor::FromHex(TEXT("FDB589")));
const FLinearColor Edge = FLinearColor(FColor::FromHex(TEXT("4E5545")));
const FLinearColor OfficerEdge = FLinearColor(FColor::FromHex(TEXT("5B4D3D")));
const FSlateRoundedBoxBrush Panel(FLinearColor(FColor::FromHex(TEXT("121911F2"))), 7.f, Edge, 1.f);
const FSlateRoundedBoxBrush OfficerPanel(FLinearColor(FColor::FromHex(TEXT("1B1611F5"))), 7.f, OfficerEdge, 1.f);
const FSlateRoundedBoxBrush Track(FLinearColor(FColor::FromHex(TEXT("4E5144"))), 2.f);
const FSlateRoundedBoxBrush Fill(FLinearColor::White, 2.f);
const FSlateRoundedBoxBrush KeyBackground(FLinearColor(FColor::FromHex(TEXT("322922"))), 3.f);
const FSlateRoundedBoxBrush NoticePin(FLinearColor::White, 3.f, FLinearColor::Black, 1.f);
const FSlateRoundedBoxBrush AimLine(Ink, 1.f, FLinearColor(0.f, 0.f, 0.f, .8f), .65f);
constexpr float OfficerPanelWidth = 396.f;
constexpr float BottomControlsHeight = 72.f;
constexpr float DecisionPanelClearance = 400.f;

TSharedRef<SWidget> MakeBattleRegimentRoster(TWeakObjectPtr<ARegimentBattleField> Field, bool bOfficerMode);

FLinearColor NoticeTeamColor(int32 Team)
{
    return Team == 0 ? Blue : Team == 1 ? Orange : Ink;
}

FText NoticeHeading(const FRegimentBattleNotice& Notice)
{
    const bool bOurs = Notice.Actor.Team == 0;
    switch (Notice.Kind)
    {
        case ERegimentBattleNotice::Flank:
            return bOurs ? LOCTEXT("NoticeFlankSuccess", "Фланговый удар удался")
                         : LOCTEXT("NoticeFlankReceived", "Удар по нашему флангу");
        case ERegimentBattleNotice::Rear:
            return bOurs ? LOCTEXT("NoticeRearSuccess", "Удар в тыл удался")
                         : LOCTEXT("NoticeRearReceived", "Наш полк атакован с тыла");
        case ERegimentBattleNotice::OfficerLost:
            return bOurs ? LOCTEXT("NoticeOwnOfficerLost", "Наш офицер выбыл")
                         : LOCTEXT("NoticeEnemyOfficerLost", "Офицер противника выбыл");
        case ERegimentBattleNotice::StandardLost:
            return bOurs ? LOCTEXT("NoticeOwnStandardLost", "Наш знаменосец выбыл")
                         : LOCTEXT("NoticeEnemyStandardLost", "Знаменосец противника выбыл");
        case ERegimentBattleNotice::Handover:
            return Notice.TransferTicks == 0 ? LOCTEXT("NoticeImmediateHandover", "Офицер выбыл · смена сразу")
                : FText::Format(LOCTEXT("NoticeHandoverDelay", "Офицер выбыл · смена через {0} с"),
                    FText::AsNumber(double(Notice.TransferTicks) * FRegimentBattlePlayback::StepSeconds));
        case ERegimentBattleNotice::Vacant:
            return bOurs ? LOCTEXT("NoticeOwnVacant", "Наш офицер выбыл · замены нет")
                         : LOCTEXT("NoticeEnemyVacant", "Офицер противника выбыл · замены нет");
        case ERegimentBattleNotice::Charge:
            if (Notice.Aspect == battle::AttackAspect::Rear)
                return bOurs ? LOCTEXT("NoticeOwnRearCharge", "Наш натиск с тыла")
                             : LOCTEXT("NoticeEnemyRearCharge", "Натиск противника с тыла");
            if (Notice.Aspect == battle::AttackAspect::Flank)
                return bOurs ? LOCTEXT("NoticeOwnFlankCharge", "Наш натиск во фланг")
                             : LOCTEXT("NoticeEnemyFlankCharge", "Натиск противника во фланг");
            return bOurs ? LOCTEXT("NoticeOwnCharge", "Наш натиск") : LOCTEXT("NoticeEnemyCharge", "Натиск противника");
        case ERegimentBattleNotice::Retreat:
            return bOurs ? LOCTEXT("NoticeRetreatOwn", "Наш полк отступает")
                         : LOCTEXT("NoticeRetreatEnemy", "Противник отступает");
        case ERegimentBattleNotice::Rout:
            return bOurs ? LOCTEXT("NoticeRoutOwn", "Наш полк бежит")
                         : LOCTEXT("NoticeRoutEnemy", "Противник бежит");
        case ERegimentBattleNotice::Rally:
            return bOurs ? LOCTEXT("NoticeRallyOwn", "Наш полк собирается")
                         : LOCTEXT("NoticeRallyEnemy", "Противник собирает полк");
        case ERegimentBattleNotice::Rallied:
            return bOurs ? LOCTEXT("NoticeRalliedOwn", "Наш полк собрался")
                         : LOCTEXT("NoticeRalliedEnemy", "Полк противника собрался");
    }
    return FText::GetEmpty();
}

FString FitNoticeName(const FText& Text, const FSlateFontMeasure& Measure, const FSlateFontInfo& Font, double Width)
{
    if (Measure.Measure(Text, Font).X <= Width)
        return Text.ToString();

    const FString Ellipsis = TEXT("…");
    const double EllipsisWidth = Measure.Measure(FStringView(Ellipsis), Font).X;
    const int32 LastCharacter = Measure.FindLastWholeCharacterIndexBeforeOffset(Text, Font,
        FMath::FloorToInt(Width - EllipsisWidth));
    return Text.ToString().Left(FMath::Max(0, LastCharacter + 1)) + Ellipsis;
}

class SRegimentNoticeLayer final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SRegimentNoticeLayer) {}
        SLATE_ARGUMENT(ARegimentBattleField*, Field)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Field = Args._Field;
        SetVisibility(EVisibility::HitTestInvisible);
        ForceVolatile(true);
    }

    FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

    int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
        FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& WidgetStyle, bool) const override
    {
        const auto* Battle = Field.Get();
        if (!Battle || Battle->GetBattleNotices().IsEmpty())
            return Layer;
        const auto* World = Battle->GetWorld();
        const auto* Controller = World ? World->GetFirstPlayerController() : nullptr;
        int32 ViewWidth = 0, ViewHeight = 0;
        if (Controller)
            Controller->GetViewportSize(ViewWidth, ViewHeight);
        if (ViewWidth <= 0 || ViewHeight <= 0)
            return Layer;

        const FVector2D Size = Geometry.GetLocalSize();
        const FVector2D ToLocal = Size / FVector2D(ViewWidth, ViewHeight);
        const auto Font = FCoreStyle::GetDefaultFontStyle("Bold", 15);
        const auto NameFont = FCoreStyle::GetDefaultFontStyle("Regular", 13);
        const auto FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        const FLinearColor PanelTint = Panel.GetTint(WidgetStyle);
        const double Bottom = Size.Y - (Battle->OfficerCommandsOpen() ? 230.0 :
            Battle->GetOfficerControlState().bActive ? 98.0 : 66.0);
        const uint64 Tick = Battle->GetBattleNoticeTick();
        TArray<FBox2D, TInlineAllocator<8>> Placed;
        const auto Notices = Battle->GetBattleNotices();
        for (int32 Index = Notices.Num() - 1; Index >= 0 && Placed.Num() < 8; --Index)
        {
            const auto& Notice = Notices[Index];
            const double Age = double(Tick - Notice.Tick) / FRegimentBattleNotices::LifetimeTicks;
            if (Age >= 1.0)
                continue;
            FVector2D Pixels;
            if (!Controller->ProjectWorldLocationToScreen(Notice.Location + FVector(0, 0, 105), Pixels, true))
                continue;
            const FVector2D Anchor = Pixels * ToLocal;
            if (Anchor.X < 8 || Anchor.X > Size.X - 8 || Anchor.Y < 84 || Anchor.Y > Bottom)
                continue;

            const FText Heading = NoticeHeading(Notice);
            const FVector2D HeadingSize = FontMeasure->Measure(Heading, Font);
            const bool bHasTarget = !Notice.Target.Name.IsEmpty();
            const FString Arrow = bHasTarget ? TEXT("  →  ") : TEXT("");
            const double ArrowWidth = FontMeasure->Measure(FStringView(Arrow), NameFont).X;
            const double TextWidth = Size.X - 40;
            const double NameWidth = FMath::Min(286.0, (TextWidth - ArrowWidth) / (bHasTarget ? 2 : 1));
            if (HeadingSize.X > TextWidth || NameWidth < 26.0)
                continue;
            const FString ActorName = FitNoticeName(Notice.Actor.Name, *FontMeasure, NameFont, NameWidth);
            const FString TargetName = bHasTarget ? FitNoticeName(Notice.Target.Name, *FontMeasure, NameFont, NameWidth) : FString();
            const FVector2D ActorSize = FontMeasure->Measure(FStringView(ActorName), NameFont);
            const FVector2D TargetSize = FontMeasure->Measure(FStringView(TargetName), NameFont);
            const double RowWidth = ActorSize.X + ArrowWidth + TargetSize.X;
            const FVector2D CardSize(FMath::Max(HeadingSize.X, RowWidth) + 24,
                HeadingSize.Y + FMath::Max(ActorSize.Y, TargetSize.Y) + 15);
            FVector2D Position(FMath::Clamp(Anchor.X - CardSize.X * .5, 8.0, Size.X - CardSize.X - 8.0),
                Anchor.Y - CardSize.Y - 22.0 - 18.0 * Age);
            FBox2D Bounds(Position, Position + CardSize);
            for (int32 Attempt = 0; Attempt < 3 && Overlaps(Placed, Bounds); ++Attempt)
            {
                Position.Y -= CardSize.Y + 5.0;
                Bounds = FBox2D(Position, Position + CardSize);
            }
            if (Position.Y < 77.0 || Overlaps(Placed, Bounds))
                continue;
            Placed.Add(Bounds);

            const float Opacity = float(1.0 - FMath::SmoothStep(.68, 1.0, Age));
            const FLinearColor Fade(1.f, 1.f, 1.f, Opacity);
            const FLinearColor ActorTint = NoticeTeamColor(Notice.Actor.Team) * Fade;
            const FLinearColor TargetTint = bHasTarget ? NoticeTeamColor(Notice.Target.Team) * Fade : ActorTint;
            const FVector2D Tail(FMath::Clamp(Anchor.X, Bounds.Min.X + 8.0, Bounds.Max.X - 8.0), Bounds.Max.Y);
            FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(),
                TArray<FVector2D>{Tail, Anchor}, ESlateDrawEffect::None, TargetTint, true, 1.5f);
            FSlateDrawElement::MakeBox(Elements, Layer + 1,
                Geometry.ToPaintGeometry(FVector2D(6, 6), FSlateLayoutTransform(Anchor - FVector2D(3, 3))),
                &NoticePin, ESlateDrawEffect::None, TargetTint);
            FSlateDrawElement::MakeBox(Elements, Layer + 1,
                Geometry.ToPaintGeometry(CardSize, FSlateLayoutTransform(Position)), &Panel,
                ESlateDrawEffect::None, PanelTint * Fade);
            FSlateDrawElement::MakeText(Elements, Layer + 2,
                Geometry.ToPaintGeometry(HeadingSize, FSlateLayoutTransform(Position + FVector2D(12, 6))),
                Heading, Font, ESlateDrawEffect::None, ActorTint);
            const FVector2D NamePosition = Position + FVector2D(12, 8 + HeadingSize.Y);
            FSlateDrawElement::MakeText(Elements, Layer + 2,
                Geometry.ToPaintGeometry(ActorSize, FSlateLayoutTransform(NamePosition)),
                ActorName, NameFont, ESlateDrawEffect::None, ActorTint);
            if (bHasTarget)
            {
                FSlateDrawElement::MakeText(Elements, Layer + 2,
                    Geometry.ToPaintGeometry(FVector2D(ArrowWidth, ActorSize.Y),
                        FSlateLayoutTransform(NamePosition + FVector2D(ActorSize.X, 0))),
                    Arrow, NameFont, ESlateDrawEffect::None, Muted * Fade);
                FSlateDrawElement::MakeText(Elements, Layer + 2,
                    Geometry.ToPaintGeometry(TargetSize,
                        FSlateLayoutTransform(NamePosition + FVector2D(ActorSize.X + ArrowWidth, 0))),
                    TargetName, NameFont, ESlateDrawEffect::None, TargetTint);
            }
        }
        return Layer + 2;
    }

private:
    static bool Overlaps(const TArray<FBox2D, TInlineAllocator<8>>& Placed, const FBox2D& Bounds)
    {
        const FBox2D Gap(Bounds.Min - FVector2D(4, 2), Bounds.Max + FVector2D(4, 2));
        return Placed.ContainsByPredicate([&Gap](const FBox2D& Card) { return Gap.Intersect(Card); });
    }

    TWeakObjectPtr<ARegimentBattleField> Field;
};

const FProgressBarStyle& StrengthStyle()
{
    static const FProgressBarStyle Style = FProgressBarStyle()
        .SetBackgroundImage(Track)
        .SetFillImage(Fill);
    return Style;
}

const FButtonStyle& ControlStyle()
{
    static const FButtonStyle Style = FButtonStyle(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
        .SetNormal(FSlateRoundedBoxBrush(FLinearColor::White * 0.65f, 3.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor::White, 8.f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor::White * 0.65f, 8.f))
        .SetNormalPadding(FMargin(0))
        .SetPressedPadding(FMargin(0, 1, 0, 0));
    return Style;
}

FText StatusText(const ARegimentBattleField* Field)
{
    if (!Field)
        return FText::GetEmpty();

    switch (Field->GetHudState().Status)
    {
        case ERegimentBattleStatus::FormingUp:
            return Field->IsBattlePaused() ? LOCTEXT("Paused", "Пауза")
                                           : LOCTEXT("FormingUp", "Перестроение");
        case ERegimentBattleStatus::Running:
            return Field->IsBattlePaused() ? LOCTEXT("Paused", "Пауза") : LOCTEXT("Running", "Бой идёт");
        case ERegimentBattleStatus::BlueWon:
            return Field->GetHudState().BlueFaction.IsEmpty() ? LOCTEXT("BlueWon", "Победа синих")
                : FText::Format(LOCTEXT("FactionWon", "Победитель — {0}"), Field->GetHudState().BlueFaction);
        case ERegimentBattleStatus::OrangeWon:
            return Field->GetHudState().OrangeFaction.IsEmpty() ? LOCTEXT("OrangeWon", "Победа оранжевых")
                : FText::Format(LOCTEXT("FactionWon", "Победитель — {0}"), Field->GetHudState().OrangeFaction);
        case ERegimentBattleStatus::Draw:
            return LOCTEXT("Draw", "Ничья");
        case ERegimentBattleStatus::Failed:
            return LOCTEXT("Failed", "Ошибка боя");
        default:
            return LOCTEXT("Preparing", "Подготовка");
    }
}

FText StrengthText(int32 Living, int32 Starting)
{
    const auto* CompactNumber = &FNumberFormattingOptions::DefaultNoGrouping();
    return FText::Format(LOCTEXT("StrengthCount", "{0} / {1}"),
        FText::AsNumber(Living, CompactNumber), FText::AsNumber(Starting, CompactNumber));
}

FText ShortFriendlyRegimentName(TWeakObjectPtr<ARegimentBattleField> Field, const FText& FullName)
{
    const auto* Battle = Field.Get();
    if (!Battle || Battle->GetHudState().BlueFaction.IsEmpty())
        return FullName;
    FString ShortName = FullName.ToString();
    const FString Prefix = Battle->GetHudState().BlueFaction.ToString() + TEXT(" · ");
    if (ShortName.RemoveFromStart(Prefix, ESearchCase::CaseSensitive) && !ShortName.IsEmpty())
        return FText::AsCultureInvariant(MoveTemp(ShortName));
    return FullName;
}

TSharedRef<SWidget> MakeRegimentPanel(TWeakObjectPtr<ARegimentBattleField> Field, bool bBlue)
{
    const FLinearColor Accent = bBlue ? Blue : Orange;
    const FText Name = bBlue ? LOCTEXT("BlueSide", "Синие") : LOCTEXT("OrangeSide", "Оранжевые");
    return SNew(SBox)
        .WidthOverride(144.f)
        .Visibility_Lambda([Field]
        {
            return Field.IsValid() && !Field->GetOfficerControlState().bActive
                ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
        })
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
            [
                SNew(STextBlock)
                .ColorAndOpacity(Accent)
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
                .Text_Lambda([Field, bBlue, Name]
                {
                    if (const auto* Battle = Field.Get())
                    {
                        const auto& State = Battle->GetHudState();
                        const FText& Faction = bBlue ? State.BlueFaction : State.OrangeFaction;
                        if (!Faction.IsEmpty())
                            return Faction;
                    }
                    return Name;
                })
                .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
            ]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 2, 0, 5)
            [
                SNew(SScaleBox).Stretch(EStretch::ScaleToFitX).StretchDirection(EStretchDirection::DownOnly)
                [
                    SNew(STextBlock)
                    .ColorAndOpacity(Accent)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 17))
                    .Text_Lambda([Field, bBlue]
                    {
                        if (const auto* Battle = Field.Get())
                        {
                            const auto& State = Battle->GetHudState();
                            return StrengthText(bBlue ? State.BlueAlive : State.OrangeAlive,
                                bBlue ? State.BlueStarting : State.OrangeStarting);
                        }
                        return FText::GetEmpty();
                    })
                ]
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBox).HeightOverride(4.f)
                [
                    SNew(SProgressBar)
                    .Style(&StrengthStyle())
                    .BorderPadding(FVector2D::ZeroVector)
                    .BarFillType(bBlue ? EProgressBarFillType::LeftToRight : EProgressBarFillType::RightToLeft)
                    .FillColorAndOpacity(Accent)
                    .Percent_Lambda([Field, bBlue]() -> TOptional<float>
                    {
                        const auto* Battle = Field.Get();
                        if (!Battle)
                            return 0.f;
                        const auto& State = Battle->GetHudState();
                        const int32 Initial = bBlue ? State.BlueStarting : State.OrangeStarting;
                        return Initial > 0 ? float(bBlue ? State.BlueAlive : State.OrangeAlive) / Initial : 0.f;
                    })
                ]
            ]
        ];
}

TSharedRef<SWidget> MakeControl(TAttribute<FText> Label, FText Tooltip, float Width,
    TAttribute<FSlateColor> Tint, FOnClicked OnClick, TAttribute<bool> Enabled = true)
{
    return SNew(SBox)
        .WidthOverride(Width)
        .HeightOverride(36.f)
        [
            SNew(SButton)
            .ButtonStyle(&ControlStyle())
            .IsFocusable(false)
            .ButtonColorAndOpacity(Tint)
            .ContentPadding(FMargin(6, 3))
            .IsEnabled(Enabled)
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            .ToolTipText(Tooltip)
            .OnClicked(OnClick)
            [
                SNew(STextBlock)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                .ColorAndOpacity(Ink)
                .Text(Label)
            ]
        ];
}

TSharedRef<SWidget> MakeSpeedControl(TWeakObjectPtr<ARegimentBattleField> Field,
    ERegimentBattleSpeed Speed, FText Label, FText Tooltip)
{
    return MakeControl(Label, Tooltip, 64.f, TAttribute<FSlateColor>::CreateLambda([Field, Speed]() -> FSlateColor
    {
        const auto* Battle = Field.Get();
        return Battle && Battle->GetBattleSpeed() == Speed
            ? SelectedTint : ControlTint;
    }), FOnClicked::CreateLambda([Field, Speed]
    {
        if (auto* Battle = Field.Get())
        {
            Battle->SetBattleSpeed(Speed);
            PlayMenuSound(Battle->GetWorld(), EMenuSound::Click);
        }
        return FReply::Handled();
    }));
}

const FRegimentOfficerHudState& OfficerState(TWeakObjectPtr<ARegimentBattleField> Field)
{
    static const FRegimentOfficerHudState Empty;
    const auto* Battle = Field.Get();
    return Battle ? Battle->GetOfficerControlState() : Empty;
}

TSharedRef<SWidget> MakePossessionEntry(TWeakObjectPtr<ARegimentBattleField> Field)
{
    return SNew(SVerticalBox)
        .Visibility_Lambda([Field]
        {
            return OfficerState(Field).bActive ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible;
        })
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
        [
            SNew(SBox)
            .Visibility_Lambda([Field]
            {
                return OfficerState(Field).bCanPossess ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
            })
            [
                MakeControl(LOCTEXT("PossessOfficer", "Вселиться в офицера"),
                    LOCTEXT("PossessOfficerHelp", "Приостановить бой и выбрать своего командира на поле (G)."),
                    220.f, FSlateColor(SelectedTint), FOnClicked::CreateLambda([Field]
                    {
                        if (auto* Battle = Field.Get())
                            Battle->OpenOfficerPicker();
                        return FReply::Handled();
                    }))
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
        [
            SNew(SBorder)
            .BorderImage(&Panel)
            .Padding(FMargin(12, 8))
            .Visibility_Lambda([Field]
            {
                return OfficerState(Field).Notice.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
            })
            [
                SNew(STextBlock)
                .ColorAndOpacity(Brass)
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
                .WrapTextAt(320.f)
                .Text_Lambda([Field] { return OfficerState(Field).Notice; })
            ]
        ];
}

TSharedRef<SWidget> MakeOfficerOrder(TWeakObjectPtr<ARegimentBattleField> Field, ERegimentPlayerOrder Order,
    const FText& Key, const FText& Label, const FText& Help)
{
    return SNew(SBox).HeightOverride(34.f)
        .Visibility_Lambda([Field, Order]
        {
            if ((Order == ERegimentPlayerOrder::CommandHold && OfficerState(Field).CommandTier < 2) ||
                (Order == ERegimentPlayerOrder::WingHold && OfficerState(Field).CommandTier < 3))
                return EVisibility::Collapsed;
            return (Order == ERegimentPlayerOrder::NextRegiment || Order == ERegimentPlayerOrder::GroupManeuver) &&
                !OfficerState(Field).bAdditionalRegiments
                ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible;
        })
        [
            SNew(SButton)
            .ButtonStyle(&ControlStyle())
            .IsFocusable(false)
            .ButtonColorAndOpacity_Lambda([Field, Order]() -> FSlateColor
            {
                return Order == ERegimentPlayerOrder::Follow &&
                        OfficerState(Field).Order.Follow != ERegimentFollowStatus::Off
                    ? SelectedTint : OfficerControlTint;
            })
            .HAlign(HAlign_Fill)
            .ContentPadding(FMargin(9, 4))
            .VAlign(VAlign_Center)
            .IsEnabled_Lambda([Field, Order]
            {
                const auto& State = OfficerState(Field);
                if (!State.bActive)
                    return false;
                if (Order == ERegimentPlayerOrder::Release)
                    return true;
                if (Order == ERegimentPlayerOrder::NextRegiment)
                    return State.bAdditionalRegiments;
                return State.bCanCommand;
            })
            .ToolTipText(Help)
            .OnClicked_Lambda([Field, Order]
            {
                if (auto* Battle = Field.Get())
                    Battle->IssueOfficerOrder(Order, /*bAimAtCursor=*/false);
                return FReply::Handled();
            })
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 7, 0)
                [
                    SNew(SBorder)
                    .BorderImage(&KeyBackground)
                    .HAlign(HAlign_Center)
                    .Padding(FMargin(5, 2))
                    [
                        SNew(STextBlock)
                        .Text(Key)
                        .ColorAndOpacity(Brass)
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
                    ]
                ]
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text_Lambda([Field, Order, Label]
                    {
                        return Order == ERegimentPlayerOrder::Pace ? OfficerState(Field).Pace : Label;
                    })
                    .ColorAndOpacity(Ink)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                ]
            ]
        ];
}

const FButtonStyle& OfficerControlStyle()
{
    static const FButtonStyle Style = FButtonStyle(ControlStyle())
        .SetNormal(FSlateRoundedBoxBrush(ControlTint, 4.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(FColor::FromHex(TEXT("4B6273"))), 4.f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(FColor::FromHex(TEXT("263944"))), 4.f));
    return Style;
}

class SRegimentPersonalKeySelector final : public SInputKeySelector
{
public:
    FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (!GetIsSelectingKey() && Event.GetEffectingButton() == EKeys::RightMouseButton)
            return FReply::Handled();
        return SInputKeySelector::OnMouseButtonDown(Geometry, Event);
    }
};

TSharedRef<SWidget> MakeOfficerControlMenu()
{
    using Action = ERegimentOfficerInputAction;
    auto Notice = MakeShared<FText>();
    auto Rows = SNew(SVerticalBox);
    for (int32 Index = 0; Index < FRegimentOfficerSettings::ActionCount; ++Index)
    {
        const auto Item = Action(Index);
        Rows->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FRegimentOfficerSettings::ActionLabel(Item))
                .ColorAndOpacity(Ink)
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SBox).WidthOverride(135.f)
                [
                    SNew(SRegimentPersonalKeySelector)
                    .ButtonStyle(&OfficerControlStyle())
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
                    .Margin(FMargin(6, 4))
                    .AllowModifierKeys(false)
                    .KeySelectionText(LOCTEXT("PressPersonalKey", "Нажмите клавишу"))
                    .SelectedKey_Lambda([Item] { return FInputChord(RegimentOfficerSettings().Key(Item)); })
                    .OnKeySelected_Lambda([Item, Notice](const FInputChord& Chord)
                    {
                        if (RegimentOfficerSettings().Bind(Item, Chord.Key))
                        {
                            *Notice = FText::GetEmpty();
                            SaveRegimentOfficerSettings();
                        }
                        else
                            *Notice = LOCTEXT("OccupiedPersonalKey", "Эта клавиша занята. Выберите другую.");
                    })
                ]
            ]
        ];
    }

    return SNew(SBorder)
        .BorderImage(&Panel)
        .Padding(15.f)
        [
            SNew(SBox).WidthOverride(330.f).MaxDesiredHeight(505.f)
            [
                SNew(SScrollBox)
                + SScrollBox::Slot().Padding(0, 0, 0, 10)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.f)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("OfficerControlsTitle", "Клавиши офицера"))
                        .ColorAndOpacity(Ink)
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 17))
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton)
                        .ButtonStyle(&OfficerControlStyle())
                        .Text(LOCTEXT("ResetPersonalControls", "Сбросить"))
                        .OnClicked_Lambda([Notice]
                        {
                            RegimentOfficerSettings() = FRegimentOfficerSettings{};
                            SaveRegimentOfficerSettings();
                            *Notice = FText::GetEmpty();
                            return FReply::Handled();
                        })
                    ]
                ]
                + SScrollBox::Slot()[Rows]
                + SScrollBox::Slot().Padding(0, 8, 0, 0)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.f)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("OfficerMouseSensitivity", "Чувствительность мыши"))
                        .ColorAndOpacity(Ink)
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(STextBlock)
                        .MinDesiredWidth(46.0f)
                        .ColorAndOpacity(Brass)
                        .Text_Lambda([] { return FText::AsPercent(RegimentOfficerSettings().Mouse()); })
                    ]
                ]
                + SScrollBox::Slot().Padding(0, 5, 0, 0)
                [
                    SNew(SSlider)
                    .MinValue(FRegimentOfficerSettings::MinMouse)
                    .MaxValue(FRegimentOfficerSettings::MaxMouse)
                    .StepSize(.05f)
                    .Value_Lambda([] { return RegimentOfficerSettings().Mouse(); })
                    .OnValueChanged_Lambda([](float Value) { RegimentOfficerSettings().SetMouse(Value); })
                    .OnMouseCaptureEnd_Static(&SaveRegimentOfficerSettings)
                    .OnControllerCaptureEnd_Static(&SaveRegimentOfficerSettings)
                ]
                + SScrollBox::Slot().Padding(0, 7, 0, 0)
                [
                    SNew(STextBlock)
                    .Text_Lambda([Notice] { return *Notice; })
                    .Visibility_Lambda([Notice] { return Notice->IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
                    .ColorAndOpacity(Brass)
                    .AutoWrapText(true)
                ]
            ]
        ];
}

TSharedRef<SWidget> MakeOfficerCommandGroup(FText Heading, const TSharedRef<SVerticalBox>& Buttons)
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
        [
            SNew(STextBlock)
            .Text(Heading)
            .ColorAndOpacity(Muted)
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
        ]
        + SVerticalBox::Slot().AutoHeight()
        [
            Buttons
        ];
}

FText OfficerDeliveryText(const FRegimentOrderPresentation& Order)
{
    switch (Order.Delivery)
    {
        case ERegimentOrderDelivery::Paused: return LOCTEXT("OrderPaused", "Приказ ждёт продолжения боя");
        case ERegimentOrderDelivery::Queued: return LOCTEXT("OrderQueued", "Приказ в очереди");
        case ERegimentOrderDelivery::InTransit: return LOCTEXT("OrderInTransit", "Приказ передаётся полку");
        case ERegimentOrderDelivery::Executing: return LOCTEXT("OrderExecuting", "Приказ получен · выполняется");
        case ERegimentOrderDelivery::Complete: return LOCTEXT("OrderComplete", "Приказ выполнен");
        case ERegimentOrderDelivery::Unapplied:
            return FText::Format(LOCTEXT("OrderUnapplied", "Исполнение не подтверждено. {0}"), Order.OrderDetail);
        default: return FText::GetEmpty();
    }
}

TSharedRef<SWidget> MakeOfficerCommandSheet(TWeakObjectPtr<ARegimentBattleField> Field)
{
    auto Common = SNew(SVerticalBox);
    auto Tactics = SNew(SVerticalBox);
    auto Movement = SNew(SVerticalBox);
    auto Navigation = SNew(SVerticalBox);
    const auto AddOrder = [Field](const TSharedRef<SVerticalBox>& Group, ERegimentPlayerOrder Order,
        const FText& Key, const FText& Label, const FText& Help)
    {
        Group->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
        [
            MakeOfficerOrder(Field, Order, Key, Label, Help)
        ];
    };
    AddOrder(Common, ERegimentPlayerOrder::CommandHold, LOCTEXT("CommandHoldKey", "J"),
        LOCTEXT("CommandHoldLabel", "Всем полкам стоять"), LOCTEXT("CommandHoldHelp", "Удерживать позицию всеми подчинёнными полками"));
    AddOrder(Common, ERegimentPlayerOrder::WingHold, LOCTEXT("WingHoldKey", "K"),
        LOCTEXT("WingHoldLabel", "Флангу стоять"), LOCTEXT("WingHoldHelp", "Приказ получит только группа, к которой относится выбранный полк"));
    AddOrder(Common, ERegimentPlayerOrder::HoldLine, LOCTEXT("GuardKey", "7"), LOCTEXT("GuardButton", "Держать строй"),
        LOCTEXT("GuardHelp", "Оборонять участок и оставаться в строю"));
    AddOrder(Common, ERegimentPlayerOrder::Follow, LOCTEXT("FollowKey", "F"),
        LOCTEXT("FollowOrder", "За мной"), LOCTEXT("FollowHelp", "Начать или прекратить движение полка за офицером"));
    AddOrder(Common, ERegimentPlayerOrder::Line, LOCTEXT("LineKey", "1"),
        LOCTEXT("LineOrder", "Линия"), LOCTEXT("LineHelp", "Развернуть полк в линию"));
    AddOrder(Common, ERegimentPlayerOrder::Withdraw, LOCTEXT("WithdrawKey", "5"),
        LOCTEXT("WithdrawOrder", "Отход"), LOCTEXT("WithdrawHelp", "Отойти из боя и собраться"));
    AddOrder(Tactics, ERegimentPlayerOrder::Hold, LOCTEXT("HoldKey", "3"),
        LOCTEXT("HoldOrder", "Стоять"), LOCTEXT("HoldHelp", "Удерживать текущую позицию"));
    AddOrder(Tactics, ERegimentPlayerOrder::Attack, LOCTEXT("AttackKey", "4"),
        LOCTEXT("AttackOrder", "Атака"), LOCTEXT("AttackHelp", "Атаковать ближайший видимый вражеский полк. Чтобы выбрать цель, наведите на неё прицел и нажмите 4."));
    AddOrder(Tactics, ERegimentPlayerOrder::Skirmishers, LOCTEXT("SkirmishersKey", "6"),
        LOCTEXT("SkirmishersOrder", "Застрельщики"), LOCTEXT("SkirmishersHelp", "Выслать стрелков или отозвать их в строй"));
    AddOrder(Tactics, ERegimentPlayerOrder::Brace, LOCTEXT("BraceKey", "8"), LOCTEXT("BraceButton", "Упор"),
        LOCTEXT("BraceHelp", "Приготовить пехоту к атаке конницы"));
    AddOrder(Tactics, ERegimentPlayerOrder::Pursue, LOCTEXT("PursueKey", "9"), LOCTEXT("PursueButton", "Погоня"),
        LOCTEXT("PursueHelp", "Продолжить бой с бегущим противником"));
    AddOrder(Movement, ERegimentPlayerOrder::Maneuver, LOCTEXT("ManeuverKey", "M"), LOCTEXT("MoveButton", "Манёвр"),
        LOCTEXT("MoveHelp", "Щёлкните по цели или протяните линию фронта"));
    AddOrder(Movement, ERegimentPlayerOrder::Column, LOCTEXT("ColumnKey", "2"),
        LOCTEXT("ColumnOrder", "Колонна"), LOCTEXT("ColumnHelp", "Выстроить полк в колонну"));
    AddOrder(Movement, ERegimentPlayerOrder::GroupManeuver, LOCTEXT("GroupManeuverKey", "H"),
        LOCTEXT("GroupMoveButton", "Группой"), LOCTEXT("GroupMoveHelp", "Переместить группу с сохранением интервалов"));
    AddOrder(Movement, ERegimentPlayerOrder::Pace, LOCTEXT("PaceKey", "T"), FText::GetEmpty(),
        LOCTEXT("PaceHelp", "Переключить шаг, марш или бег"));
    AddOrder(Movement, ERegimentPlayerOrder::Cover, LOCTEXT("CoverKey", "B"), LOCTEXT("CoverButton", "В укрытие"),
        LOCTEXT("CoverHelp", "Выберите площадку редута, каменную стену или изгородь"));
    AddOrder(Movement, ERegimentPlayerOrder::FireSector, LOCTEXT("FireSectorKey", "Y"), LOCTEXT("FireSectorButton", "Сектор"),
        LOCTEXT("FireSectorHelp", "Показать сектор стрелков и препятствия на линии огня"));


    AddOrder(Navigation, ERegimentPlayerOrder::NextRegiment, LOCTEXT("NextRegimentKey", "R"),
        LOCTEXT("NextRegimentOrder", "Сменить полк"),
        LOCTEXT("NextRegimentHelp", "Выбрать следующий подчинённый полк"));
    Navigation->AddSlot().AutoHeight().Padding(0, 0, 0, 3)
    [
        MakeControl(LOCTEXT("ChooseOtherOfficer", "Сменить офицера"),
            LOCTEXT("ChooseOtherHelp", "Остановить бой и выбрать другого командира"),
            145.f, FSlateColor(OfficerControlTint), FOnClicked::CreateLambda([Field]
            {
                if (auto* Battle = Field.Get()) Battle->OpenOfficerPicker();
                return FReply::Handled();
            }))
    ];
    AddOrder(Navigation, ERegimentPlayerOrder::Release, LOCTEXT("ReleaseKey", "G"),
        LOCTEXT("ReleaseOrder", "Общий вид"),
        LOCTEXT("ReleaseHelp", "Вернуться к обзору поля и передать управление офицером ИИ"));

    return SNew(SBox)
        .WidthOverride(OfficerPanelWidth)
        .Visibility_Lambda([Field]
        {
            return Field.IsValid() && Field->OfficerCommandsOpen() && OfficerState(Field).bActive
                ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
        })
        [
            SNew(SBorder)
            .BorderImage(&OfficerPanel)
            .Padding(FMargin(12, 10))
            .Clipping(EWidgetClipping::ClipToBounds)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 7)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                    [
                        SNew(STextBlock).Text_Lambda([Field] { return OfficerState(Field).CommandTitle; })
                        .ColorAndOpacity(Ink).Font(FCoreStyle::GetDefaultFontStyle("Bold", 17))
                        .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton)
                        .ButtonStyle(&ControlStyle()).IsFocusable(false)
                        .ButtonColorAndOpacity(FSlateColor(OfficerControlTint))
                        .ContentPadding(FMargin(8, 6))
                        .ToolTipText(LOCTEXT("OfficerMotionHelp", "Tab или Esc — вернуться к управлению офицером"))
                        .OnClicked_Lambda([Field]
                        {
                            if (auto* Battle = Field.Get())
                                Battle->ToggleOfficerCommands();
                            return FReply::Handled();
                        })
                        [
                            SNew(STextBlock).Text(LOCTEXT("CloseOfficerOrders", "Tab · Закрыть"))
                            .ColorAndOpacity(Brass).Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
                        ]
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
                [
                    SNew(STextBlock)
                    .Text_Lambda([Field] { return FText::Format(LOCTEXT("SelectedRecipient", "Выбран · {0}"), ShortFriendlyRegimentName(Field, OfficerState(Field).Regiment)); })
                    .ToolTipText_Lambda([Field] { return OfficerState(Field).CommandScope; })
                    .ColorAndOpacity(Blue).Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 7)
                [
                    SNew(STextBlock)
                    .Text_Lambda([Field]
                    {
                        const auto& Order = OfficerState(Field).Order;
                        return Order.Activity.IsEmpty() ? LOCTEXT("OfficerNoOrder", "Ожидает приказа") : Order.Activity;
                    })
                    .ColorAndOpacity(Muted).Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                [
                    SNew(SSeparator).Thickness(1.f).ColorAndOpacity(OfficerEdge)
                ]
                + SVerticalBox::Slot().FillHeight(1.f)
                [
                    SNew(SBox).HeightOverride(252.f)
                    [
                        SNew(SScrollBox).AllowOverscroll(EAllowOverscroll::No)
                        .ScrollBarThickness(FVector2D(5, 5))
                        .ConsumeMouseWheel(EConsumeMouseWheel::Always)
                        + SScrollBox::Slot().Padding(0, 0, 4, 8)
                        [
                            MakeOfficerCommandGroup(LOCTEXT("CommonOrderGroup", "Основные"), Common)
                        ]
                        + SScrollBox::Slot().Padding(0, 0, 4, 8)
                        [
                            MakeOfficerCommandGroup(LOCTEXT("TacticsGroup", "Тактика"), Tactics)
                        ]
                        + SScrollBox::Slot().Padding(0, 0, 4, 8)
                        [
                            MakeOfficerCommandGroup(LOCTEXT("ManeuverGroup", "Манёвр"), Movement)
                        ]
                        + SScrollBox::Slot().Padding(0, 0, 4, 8)
                        [
                            MakeOfficerCommandGroup(LOCTEXT("OfficerNavigationGroup", "Полк и камера"), Navigation)
                        ]
                        + SScrollBox::Slot().Padding(0, 0, 4, 6)
                        [
                            MakeBattleRegimentRoster(Field, true)
                        ]
                        + SScrollBox::Slot().HAlign(HAlign_Left).Padding(0, 0, 4, 0)
                        [
                            SNew(SComboButton)
                            .ButtonStyle(&OfficerControlStyle())
                            .ContentPadding(FMargin(9, 4))
                            .MenuPlacement(MenuPlacement_AboveAnchor)
                            .OnGetMenuContent_Static(&MakeOfficerControlMenu)
                            .ButtonContent()
                            [
                                SNew(STextBlock)
                                .Text(LOCTEXT("OfficerControlsButton", "Управление"))
                                .ColorAndOpacity(Ink)
                                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                            ]
                        ]
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
                [
                    SNew(STextBlock)
                    .Text_Lambda([Field] { return OfficerDeliveryText(OfficerState(Field).Order); })
                    .Visibility_Lambda([Field]
                    {
                        return OfficerState(Field).Order.Delivery == ERegimentOrderDelivery::None
                            ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
                    })
                    .ToolTipText_Lambda([Field] { return OfficerDeliveryText(OfficerState(Field).Order); })
                    .ColorAndOpacity(Brass).Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                ]
            ]
        ];
}

FText OfficerKeyHint(ERegimentOfficerInputAction Item)
{
    const auto& Key = RegimentOfficerSettings().Key(Item);
    if (Key == EKeys::LeftMouseButton)
        return LOCTEXT("LeftMouseShort", "ЛКМ");
    if (Key == EKeys::RightMouseButton)
        return LOCTEXT("RightMouseShort", "ПКМ");
    if (Key == EKeys::SpaceBar)
        return LOCTEXT("SpaceShort", "Пробел");
    if (Key == EKeys::LeftShift || Key == EKeys::RightShift)
        return LOCTEXT("ShiftShort", "Shift");
    return Key.GetDisplayName(false);
}

FText PersonalControlHints(bool bFirstPerson)
{
    using Action = ERegimentOfficerInputAction;
    const auto& Keys = RegimentOfficerSettings();
    const FText Walk = Keys.Key(Action::Forward) == EKeys::W && Keys.Key(Action::Left) == EKeys::A &&
        Keys.Key(Action::Backward) == EKeys::S && Keys.Key(Action::Right) == EKeys::D
        ? LOCTEXT("DefaultWalkKeys", "WASD")
        : FText::Format(LOCTEXT("CustomWalkKeys", "{0}/{1}/{2}/{3}"),
            OfficerKeyHint(Action::Forward), OfficerKeyHint(Action::Left), OfficerKeyHint(Action::Backward), OfficerKeyHint(Action::Right));
    return FText::Format(bFirstPerson ? LOCTEXT("FirstPersonMotionHints", "{0} — шаг  ·  {1} — бег  ·  {2} — прыжок")
        : LOCTEXT("ShoulderMotionHints", "{0} — шаг  ·  {1} — бег  ·  {2} — прыжок\nКолесо — дистанция"),
        Walk, OfficerKeyHint(Action::Run), OfficerKeyHint(Action::Jump));
}

TSharedRef<SWidget> MakeCompactOfficerHud(TWeakObjectPtr<ARegimentBattleField> Field)
{
    return SNew(SBox).WidthOverride(308.f)
        .Visibility_Lambda([Field]
        {
            return Field.IsValid() && OfficerState(Field).bActive
                ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
        })
        [
            SNew(SBorder).BorderImage(&OfficerPanel).Padding(FMargin(12, 9))
            .Clipping(EWidgetClipping::ClipToBounds)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                    [
                        SNew(SRegimentCommanderAvatar).Size(46.f)
                        .Portrait_Lambda([Field] { return OfficerState(Field).Portrait; })
                        .Rank_Lambda([Field] { return OfficerState(Field).RankIcon; })
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                        [
                            SNew(STextBlock).Text_Lambda([Field] { return OfficerState(Field).Officer; })
                            .ColorAndOpacity(Blue).Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))
                            .AutoWrapText(true)
                        ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
                        [
                            SNew(STextBlock).Text_Lambda([Field] { return OfficerState(Field).Regiment; })
                            .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                            .ColorAndOpacity(Ink).Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
                        ]
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
                [
                    SNew(SHorizontalBox)
                    .Visibility_Lambda([Field]
                    {
                        return OfficerState(Field).HealthFraction.IsSet()
                            ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                    })
                    + SHorizontalBox::Slot().FillWidth(1.f)
                    [
                        SNew(STextBlock).Text(LOCTEXT("OfficerHealth", "Здоровье"))
                        .ColorAndOpacity(Ink).Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(STextBlock)
                        .Text_Lambda([Field] { return FText::AsPercent(OfficerState(Field).HealthFraction.Get(0.f)); })
                        .ColorAndOpacity(Brass).Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 6)
                [
                    SNew(SBox).HeightOverride(4.f)
                    .Visibility_Lambda([Field]
                    {
                        return OfficerState(Field).HealthFraction.IsSet()
                            ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                    })
                    [
                        SNew(SProgressBar)
                        .Style(&StrengthStyle())
                        .BorderPadding(FVector2D::ZeroVector)
                        .FillColorAndOpacity(Brass)
                        .Percent_Lambda([Field] { return OfficerState(Field).HealthFraction; })
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock)
                    .Visibility_Lambda([Field] { return OfficerState(Field).RegimentCondition.IsEmpty()
                        ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
                    .Text_Lambda([Field] { return OfficerState(Field).RegimentCondition; })
                    .ColorAndOpacity(Muted).Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
                    .AutoWrapText(true)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 7, 0, 0)
                [
                    SNew(SVerticalBox)
                    .Visibility_Lambda([Field]
                    {
                        const auto* Battle = Field.Get();
                        return Battle && Battle->OfficerCommandsOpen() && !OfficerState(Field).ProfileEffects.IsEmpty()
                            ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                    })
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(STextBlock).Text_Lambda([Field] { return OfficerState(Field).TacticalSkill; })
                        .AutoWrapText(true).ColorAndOpacity(Brass).Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
                    [
                        SNew(STextBlock).Text_Lambda([Field] { return OfficerState(Field).ProfileEffects; })
                        .AutoWrapText(true).LineHeightPercentage(1.17f)
                        .ColorAndOpacity(Ink).Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 7, 0, 0)
                [
                    SNew(STextBlock)
                    .Text_Lambda([Field]
                    {
                        return OfficerState(Field).bPlanning
                            ? LOCTEXT("OfficerPlotControls", "ЛКМ — указать позицию   ·   Esc — отменить")
                            : PersonalControlHints(OfficerState(Field).bFirstPerson);
                    })
                    .ColorAndOpacity(Ink).Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                    .AutoWrapText(true)
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
                [
                    SNew(STextBlock)
                    .Text_Lambda([Field] { return OfficerState(Field).Notice; })
                    .Visibility_Lambda([Field]
                    {
                        return OfficerState(Field).Notice.IsEmpty()
                            ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
                    })
                    .ColorAndOpacity(Brass).Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
                    .AutoWrapText(true)
                ]
            ]
        ];
}

TSharedRef<SWidget> MakeOfficerCombatHints(TWeakObjectPtr<ARegimentBattleField> Field)
{
    using Action = ERegimentOfficerInputAction;
    const auto ActionPrompt = [Field](Action Item)
    {
        return SNew(SBorder).BorderImage(&OfficerPanel).Padding(FMargin(8, 6))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
                [
                    SNew(SBorder).BorderImage(&KeyBackground).Padding(FMargin(4, 1))
                    [
                        SNew(STextBlock).Text_Lambda([Item] { return OfficerKeyHint(Item); })
                        .ColorAndOpacity(Brass).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
                    ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(STextBlock).Text_Lambda([Field, Item]
                    {
                        if (Item == Action::Attack)
                            return OfficerState(Field).FieldGunStatus.IsEmpty()
                                ? LOCTEXT("PersonalAttackHint", "Удар")
                                : FText::Format(LOCTEXT("FieldGunAttackHint", "Огонь из орудия · {0}"),
                                    OfficerState(Field).FieldGunStatus);
                        return OfficerState(Field).bParryActive ? LOCTEXT("PersonalGuardActive", "В защите")
                                                               : LOCTEXT("PersonalGuardHint", "Парирование");
                    })
                    .ColorAndOpacity_Lambda([Field, Item]() -> FSlateColor
                    {
                        const auto& State = OfficerState(Field);
                        if (Item == Action::Parry && State.bParryActive)
                            return Blue;
                        const bool bReady = Item == Action::Attack ? State.bAttackReady : State.bParryReady;
                        return bReady ? Ink : Muted;
                    })
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
                ]
            ];
    };
    return SNew(SHorizontalBox)
        .Visibility_Lambda([Field]
        {
            const auto* Battle = Field.Get();
            return Battle && OfficerState(Field).bActive && !OfficerState(Field).bPlanning &&
                !Battle->IsBattlePaused() && !Battle->OfficerCommandsOpen()
                ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
        })
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)[ActionPrompt(Action::Attack)]
        + SHorizontalBox::Slot().AutoWidth()[ActionPrompt(Action::Parry)];
}


class SRegimentRoster final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SRegimentRoster) {}
        SLATE_ARGUMENT(TWeakObjectPtr<ARegimentBattleField>, Field)
        SLATE_ARGUMENT(bool, OfficerMode)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        Field = InArgs._Field;
        bOfficerMode = InArgs._OfficerMode;
        SetVisibility(EVisibility::SelfHitTestInvisible);
        if (bOfficerMode)
        {
            ChildSlot
            [
                SNew(SExpandableArea).InitiallyCollapsed(true).AllowAnimatedTransition(false)
                .HeaderContent()
                [
                    SNew(STextBlock).Text(LOCTEXT("OfficerBattleRegiments", "Подразделения"))
                    .ColorAndOpacity(Brass).Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                ]
                .BodyContent()
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[MakeEmptyState()]
                    + SVerticalBox::Slot().AutoHeight()[SAssignNew(OfficerCards, SVerticalBox)]
                ]
            ];
        }
        else
        {
            ChildSlot
            [
                SNew(SBorder).BorderImage(&Panel).Padding(FMargin(9))
                .Visibility(EVisibility::SelfHitTestInvisible)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
                    [
                        SNew(SHorizontalBox)
                        .Visibility_Lambda([this]
                        {
                            return FindCard(OverviewSelection) ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
                        })
                        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0, 0, 7, 0)
                        [
                            SNew(STextBlock).Text_Lambda([this] { return OverviewDetails(); })
                            .ToolTipText_Lambda([this] { return OverviewDetails(); })
                            .ColorAndOpacity(Ink).Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
                            .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            MakeOfficerTransfer(0)
                        ]
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SAssignNew(OverviewCards, SScrollBox).Orientation(Orient_Horizontal)
                        .AllowOverscroll(EAllowOverscroll::No)
                        .ScrollBarThickness(FVector2D(5, 5))
                        .ScrollBarPadding(FMargin(0, 4, 0, 0))
                        .Visibility_Lambda([this]
                        {
                            return Ids.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
                        })
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(3, 3)
                    [
                        MakeEmptyState()
                    ]
                ]
            ];
        }
        RefreshCards();
    }

    virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override
    {
        SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
        RefreshCards();
    }

private:
    TConstArrayView<FRegimentFieldCard> ReadCards() const
    {
        const auto* Battle = Field.Get();
        return Battle ? Battle->GetRegimentCards() : TConstArrayView<FRegimentFieldCard>();
    }

    const FRegimentFieldCard* FindCard(uint64 Id) const
    {
        return ReadCards().FindByPredicate([Id](const auto& Card) { return Card.Id == Id; });
    }

    FText OverviewDetails() const
    {
        const auto* Card = FindCard(OverviewSelection);
        if (!Card)
            return FText::GetEmpty();
        const auto Summary = FText::Format(LOCTEXT("RegimentDetails", "{0} · {1} · Дух {2}% · Усталость {3}%"),
            Card->Name, Card->Activity, Card->Morale, Card->Fatigue);
        return Card->bFiniteAmmo
            ? FText::Format(LOCTEXT("RegimentDetailsAmmo", "{0} · Боезапас {1}"), Summary, Card->Ammo)
            : Summary;
    }

    TSharedRef<SWidget> MakeEmptyState()
    {
        return SNew(STextBlock).Text(LOCTEXT("BattleRosterEmpty", "Нет полков для выбора"))
            .Visibility_Lambda([this] { return Ids.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
            .ColorAndOpacity(Muted).Font(FCoreStyle::GetDefaultFontStyle("Regular", 13));
    }

    TSharedRef<SWidget> MakeOfficerTransfer(uint64 Id)
    {
        const auto ReadTarget = [this, Id] { return Id ? Id : OverviewSelection; };
        return SNew(SButton)
            .ButtonStyle(&ControlStyle()).IsFocusable(false)
            .ButtonColorAndOpacity(bOfficerMode ? OfficerControlTint : ControlTint)
            .ContentPadding(FMargin(7, 5))
            .IsEnabled_Lambda([this, ReadTarget]
            {
                const auto* Battle = Field.Get();
                const auto* Card = FindCard(ReadTarget());
                return Battle && Card && Card->bCanEnterOfficer && !Battle->IsCardOfficerQueued(Card->Id);
            })
            .ToolTipText(LOCTEXT("CardOfficerTip", "Приостановить бой и открыть выбор с выделением командира этого полка."))
            .OnClicked_Lambda([this, ReadTarget]
            {
                if (auto* Battle = Field.Get())
                    Battle->EnterRegimentCardOfficer(ReadTarget());
                return FReply::Handled();
            })
            [
                SNew(STextBlock).Text_Lambda([this, ReadTarget]
                {
                    const auto* Battle = Field.Get();
                    return Battle && Battle->IsCardOfficerQueued(ReadTarget())
                        ? LOCTEXT("CardOfficerQueued", "Выбран")
                        : LOCTEXT("CardOfficerButton", "К офицеру");
                }).ColorAndOpacity(Brass).Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
            ];
    }

    TSharedRef<SWidget> MakeCard(uint64 Id)
    {
        const auto Read = [this, Id] { return FindCard(Id); };
        const auto Selected = [this, Id]
        {
            const auto* Battle = Field.Get();
            return bOfficerMode ? Battle && Battle->GetSelectedRegimentCard() == Id : OverviewSelection == Id;
        };
        return SNew(SButton)
            .ButtonStyle(&ControlStyle()).IsFocusable(false)
            .HAlign(HAlign_Fill).ContentPadding(FMargin(9, 6))
            .IsEnabled_Lambda([this, Read]
            {
                const auto* Card = Read();
                return Card && (!bOfficerMode || (Card->Living > 0 && Card->bInCommand));
            })
            .ToolTipText_Lambda([Read] { const auto* Card = Read(); return Card ? Card->Name : FText::GetEmpty(); })
            .ButtonColorAndOpacity_Lambda([this, Selected]() -> FSlateColor
            {
                if (Selected())
                    return bOfficerMode ? SelectedTint : RosterAccent;
                return bOfficerMode ? OfficerControlTint : ControlTint;
            })
            .OnClicked_Lambda([this, Id]
            {
                if (auto* Battle = Field.Get())
                {
                    if (bOfficerMode)
                        Battle->SelectRegimentCard(Id);
                    else
                        OverviewSelection = Id;
                }
                return FReply::Handled();
            })
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([Read]
                        {
                            const auto* Card = Read();
                            return Card ? FText::Format(LOCTEXT("RosterRegimentNumber", "№{0}"), Card->Id)
                                        : FText::GetEmpty();
                        })
                        .ColorAndOpacity_Lambda([this, Selected]() -> FSlateColor
                        {
                            return !bOfficerMode && Selected() ? RosterInk : Brass;
                        })
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this, Read]
                        {
                            const auto* Card = Read();
                            return Card ? ShortFriendlyRegimentName(Field, Card->Name) : FText::GetEmpty();
                        })
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                        .ColorAndOpacity_Lambda([this, Selected]() -> FSlateColor
                        {
                            return !bOfficerMode && Selected() ? RosterInk : Ink;
                        })
                        .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 3)
                [
                    SNew(STextBlock)
                    .Text_Lambda([Read]
                    {
                        const auto* Card = Read();
                        return Card ? FText::Format(LOCTEXT("RosterLiving", "{0} в строю"), StrengthText(Card->Living, Card->Starting))
                                    : FText::GetEmpty();
                    })
                    .ColorAndOpacity_Lambda([this, Selected]() -> FSlateColor
                    {
                        return !bOfficerMode && Selected() ? RosterInk : Muted;
                    })
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SBox).HeightOverride(3.f)
                    [
                        SNew(SProgressBar).Style(&StrengthStyle()).BorderPadding(FVector2D::ZeroVector)
                        .FillColorAndOpacity_Lambda([this, Selected]() -> FSlateColor
                        {
                            return !bOfficerMode && Selected() ? RosterInk : RosterAccent;
                        })
                        .Percent_Lambda([Read]() -> TOptional<float>
                        {
                            const auto* Card = Read();
                            return Card && Card->Starting > 0 ? FMath::Clamp(float(Card->Living) / Card->Starting, 0.f, 1.f) : 0.f;
                        })
                    ]
                ]
            ];
    }

    void RefreshCards()
    {
        const auto Cards = ReadCards();
        if (Ids.Num() == Cards.Num())
        {
            int32 Index = 0;
            while (Index < Cards.Num() && Ids[Index] == Cards[Index].Id)
                ++Index;
            if (Index == Cards.Num())
                return;
        }

        if (!FindCard(OverviewSelection))
        {
            const auto* Living = Cards.FindByPredicate([](const auto& Card) { return Card.Living > 0; });
            OverviewSelection = Living ? Living->Id : !Cards.IsEmpty() ? Cards[0].Id : 0;
        }
        Ids.Reset(Cards.Num());
        if (bOfficerMode)
            OfficerCards->ClearChildren();
        else
            OverviewCards->ClearChildren();
        for (const auto& Card : Cards)
        {
            Ids.Add(Card.Id);
            if (bOfficerMode)
            {
                OfficerCards->AddSlot().AutoHeight().Padding(0, 2, 0, 2)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.f)[MakeCard(Card.Id)]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
                    [
                        MakeOfficerTransfer(Card.Id)
                    ]
                ];
            }
            else
            {
                OverviewCards->AddSlot().Padding(0, 0, 5, 0)
                [
                    SNew(SBox).WidthOverride(148.f)[MakeCard(Card.Id)]
                ];
            }
        }
    }

    TWeakObjectPtr<ARegimentBattleField> Field;
    TSharedPtr<SScrollBox> OverviewCards;
    TSharedPtr<SVerticalBox> OfficerCards;
    TArray<uint64> Ids;
    uint64 OverviewSelection = 0;
    bool bOfficerMode = false;
};

TSharedRef<SWidget> MakeBattleRegimentRoster(TWeakObjectPtr<ARegimentBattleField> Field, bool bOfficerMode)
{
    return SNew(SRegimentRoster).Field(Field).OfficerMode(bOfficerMode);
}

bool ShowPlaybackControls(TWeakObjectPtr<ARegimentBattleField> Field)
{
    const auto* Battle = Field.Get();
    return Battle && (!OfficerState(Field).bActive || Battle->OfficerCommandsOpen() || Battle->IsBattlePaused());
}

TSharedRef<SWidget> MakeOfficerCommandHint(TWeakObjectPtr<ARegimentBattleField> Field)
{
    return SNew(SHorizontalBox)
        .Visibility_Lambda([Field]
        {
            return Field.IsValid() && OfficerState(Field).bActive && !OfficerState(Field).bPlanning
                ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
        })
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
        [
            MakeControl(TAttribute<FText>::CreateLambda([Field]
            {
                const auto Key = OfficerKeyHint(ERegimentOfficerInputAction::Camera);
                return FText::Format(OfficerState(Field).bFirstPerson
                    ? LOCTEXT("CameraFromEyes", "{0} · От первого лица")
                    : LOCTEXT("CameraOverShoulder", "{0} · От плеча"), Key);
            }), LOCTEXT("SwitchOfficerView", "Переключить вид от первого лица и вид от плеча"),
                172.f, FSlateColor(OfficerControlTint), FOnClicked::CreateLambda([Field]
                {
                    if (auto* Battle = Field.Get())
                        Battle->ToggleOfficerView();
                    return FReply::Handled();
                }))
        ]
        + SHorizontalBox::Slot().AutoWidth()
        [
            SNew(SBox).Visibility_Lambda([Field]
            {
                return Field.IsValid() && !Field->OfficerCommandsOpen()
                    ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
            })
            [
                MakeControl(LOCTEXT("OfficerCommandsPrompt", "Tab · Приказы"),
                    LOCTEXT("OfficerCommandsPromptHelp", "Открыть приказы клавишей Tab"),
                    132.f, FSlateColor(OfficerControlTint), FOnClicked::CreateLambda([Field]
                    {
                        if (auto* Battle = Field.Get())
                            Battle->ToggleOfficerCommands();
                        return FReply::Handled();
                    }))
            ]
        ];
}

class SRegimentTutorialMarker final : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SRegimentTutorialMarker) {}
        SLATE_ARGUMENT(TWeakObjectPtr<ARegimentBattleField>, Field)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Field = Args._Field;
        SetVisibility(EVisibility::HitTestInvisible);
        ForceVolatile(true);
    }

    FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

    int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Elements,
        int32 Layer, const FWidgetStyle&, bool) const override
    {
        FVector Center;
        float Radius = 0.f;
        const auto* Battle = Field.Get();
        if (!Battle || !Battle->GetTutorialMarker(Center, Radius))
            return Layer;
        const auto* World = Battle->GetWorld();
        const auto* Controller = World ? World->GetFirstPlayerController() : nullptr;
        if (!Controller)
            return Layer;
        int32 Width = 0, Height = 0;
        Controller->GetViewportSize(Width, Height);
        if (Width <= 0 || Height <= 0)
            return Layer;

        const FVector2D Scale = Geometry.GetLocalSize() / FVector2D(Width, Height);
        const auto Project = [&](FVector Point, FVector2D& Projected)
        {
            if (!Controller->ProjectWorldLocationToScreen(Point, Projected, true) || Projected.ContainsNaN())
                return false;
            Projected *= Scale;
            return true;
        };
        const auto Stroke = [&](const TArray<FVector2D>& Points, float Weight)
        {
            FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Points,
                ESlateDrawEffect::None, FLinearColor(.04f, .027f, .013f, .9f), true, Weight + 2.f);
            FSlateDrawElement::MakeLines(Elements, Layer + 1, Geometry.ToPaintGeometry(), Points,
                ESlateDrawEffect::None, Brass, true, Weight);
        };

        constexpr int32 Sides = 32;
        TArray<FVector2D> Ring;
        Ring.Reserve(Sides + 1);
        for (int32 Side = 0; Side <= Sides; ++Side)
        {
            const double Angle = UE_DOUBLE_TWO_PI * Side / Sides;
            const FVector Point = Center + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 9);
            FVector2D Projected;
            if (!Project(Point, Projected))
            {
                Ring.Reset();
                break;
            }
            Ring.Add(Projected);
        }
        if (Ring.Num() > 2)
            Stroke(Ring, 2.f);

        FVector2D Pin;
        if (Project(Center + FVector(0, 0, 180), Pin))
        {
            constexpr double R = 7.0;
            Stroke({ Pin + FVector2D(0, -R), Pin + FVector2D(R, 0), Pin + FVector2D(0, R),
                Pin + FVector2D(-R, 0), Pin + FVector2D(0, -R) }, 2.f);
        }
        return Layer + 1;
    }

private:
    TWeakObjectPtr<ARegimentBattleField> Field;
};

TSharedRef<SWidget> MakeMissionCard(TWeakObjectPtr<ARegimentBattleField> Field)
{
    return SNew(SBox).MaxDesiredWidth(292.f)
        .Visibility_Lambda([Field]
        {
            const auto* Battle = Field.Get();
            return Battle && !Battle->GetMissionObjectiveText().IsEmpty()
                ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
        })
        [
            SNew(SBorder)
            .BorderImage_Lambda([Field] { return OfficerState(Field).bActive ? &OfficerPanel : &Panel; })
            .Padding(FMargin(11, 9))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock)
                    .Text_Lambda([Field]
                    {
                        const auto* Battle = Field.Get();
                        return Battle ? Battle->GetMissionObjectiveText() : FText::GetEmpty();
                    })
                    .AutoWrapText(true).ColorAndOpacity(Ink)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
                [
                    SNew(STextBlock)
                    .Visibility_Lambda([Field]
                    {
                        const auto* Battle = Field.Get();
                        return Battle && (!OfficerState(Field).bActive || Battle->OfficerCommandsOpen()) &&
                            !Battle->GetMissionReport().IsEmpty()
                            ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                    })
                    .Text_Lambda([Field]
                    {
                        const auto* Battle = Field.Get();
                        return Battle ? Battle->GetMissionReport() : FText::GetEmpty();
                    })
                    .AutoWrapText(true).ColorAndOpacity(Muted)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
                ]
            ]
        ];
}

TSharedRef<SWidget> MakeBattleScore(TWeakObjectPtr<ARegimentBattleField> Field)
{
    return SNew(SBorder)
        .Visibility(EVisibility::HitTestInvisible)
        .BorderImage_Lambda([Field] { return OfficerState(Field).bActive ? &OfficerPanel : &Panel; })
        .Padding(FMargin(12, 8))
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()[MakeRegimentPanel(Field, true)]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride_Lambda([Field]
                {
                    const auto* Battle = Field.Get();
                    return FOptionalSize(OfficerState(Field).bActive && Battle &&
                        Battle->GetHudState().Status == ERegimentBattleStatus::Running ? 98.f : 132.f);
                })
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                    [
                        SNew(STextBlock)
                        .ColorAndOpacity(Brass)
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 22))
                        .Text_Lambda([Field]
                        {
                            const auto* Battle = Field.Get();
                            const uint64 Seconds = Battle ? Battle->GetHudState().ElapsedSeconds : 0;
                            return FText::AsCultureInvariant(FString::Printf(TEXT("%02llu:%02llu"),
                                static_cast<unsigned long long>(Seconds / 60),
                                static_cast<unsigned long long>(Seconds % 60)));
                        })
                    ]
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 3, 0, 0)
                    [
                        SNew(STextBlock)
                        .ColorAndOpacity(Muted)
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
                        .Text_Lambda([Field] { return StatusText(Field.Get()); })
                        .AutoWrapText(true).Justification(ETextJustify::Center)
                    ]
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth()[MakeRegimentPanel(Field, false)]
        ];
}

}

void SRegimentBattleHud::Construct(const FArguments& InArgs)
{
    const TWeakObjectPtr<ARegimentBattleField> Field(InArgs._Field);
    const TWeakObjectPtr<UWorld> World(Field.IsValid() ? Field->GetWorld() : nullptr);
    SetVisibility(EVisibility::SelfHitTestInvisible);

    ChildSlot
    [
        SNew(SOverlay)
        .Visibility(EVisibility::SelfHitTestInvisible)
        + SOverlay::Slot()
        [
        SNew(SOverlay)
        .Visibility_Lambda([Field]
        {
            return Field.IsValid() && Field->IsChoosingOfficer() ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible;
        })
        + SOverlay::Slot()
        [
            SNew(SRegimentFieldOverlay).Owner(InArgs._Field)
            .Lines([Field]
            {
                return Field.IsValid() ? Field->GetFieldLines() : TConstArrayView<FRegimentFieldLine>();
            })
            .Cards([Field]
            {
                return Field.IsValid() ? Field->GetRegimentCards() : TConstArrayView<FRegimentFieldCard>();
            })
        ]
        + SOverlay::Slot()
        [
            SNew(SRegimentTutorialMarker).Field(Field)
        ]
        + SOverlay::Slot()
        [
            SNew(SRegimentNoticeLayer).Field(InArgs._Field)
        ]
        + SOverlay::Slot()
        [
            SNew(SRegimentCommandPanel).Field(InArgs._Field)
        ]
        + SOverlay::Slot().VAlign(VAlign_Top).Padding(18, 14, 18, 0)
        [
            SNew(SHorizontalBox).Visibility(EVisibility::SelfHitTestInvisible)
            + SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(0, 0, 8, 0)
            [
                MakeMissionCard(Field)
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
            [
                MakeBattleScore(Field)
            ]
            + SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(8, 0, 0, 0)
            [
                SNew(SScaleBox).Stretch(EStretch::ScaleToFitX).StretchDirection(EStretchDirection::DownOnly)
                .Visibility(EVisibility::SelfHitTestInvisible)
                [
                    SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
                    + SOverlay::Slot()[MakePossessionEntry(Field)]
                    + SOverlay::Slot()[MakeOfficerCommandHint(Field)]
                ]
            ]
        ]
        + SOverlay::Slot().VAlign(VAlign_Bottom).Padding(18, 0, 18, BottomControlsHeight)
        [
            SNew(SHorizontalBox)
            .Visibility_Lambda([Field]
            {
                return Field.IsValid() && !OfficerState(Field).bActive
                    ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
            })
            + SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Left)
            [
                SNew(SBox).MaxDesiredWidth(784.f).Visibility(EVisibility::SelfHitTestInvisible)
                [
                    MakeBattleRegimentRoster(Field, false)
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SBox).WidthOverride(DecisionPanelClearance).Visibility(EVisibility::HitTestInvisible)
                [SNullWidget::NullWidget]
            ]
        ]
        + SOverlay::Slot().Padding(TAttribute<FMargin>::CreateLambda([Field]
        {
            return FMargin(18, 82, 18, ShowPlaybackControls(Field) ? BottomControlsHeight : 18);
        }))
        [
            SNew(SHorizontalBox)
            .Visibility(EVisibility::SelfHitTestInvisible)
            + SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(0, 0, 8, 0)
            [
                MakeCompactOfficerHud(Field)
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom)
            [
                MakeOfficerCombatHints(Field)
            ]
            + SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(8, 0, 0, 0)
            [
                MakeOfficerCommandSheet(Field)
            ]
        ]
        + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
        [
            SNew(SBox).WidthOverride(14.f).HeightOverride(14.f)
            .Visibility_Lambda([Field]
            {
                const auto& State = OfficerState(Field);
                return Field.IsValid() && State.bActive && !State.bPlanning &&
                    !Field->IsBattlePaused() && !Field->OfficerCommandsOpen()
                    ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
            })
            [
                SNew(SOverlay)
                + SOverlay::Slot().VAlign(VAlign_Center)
                [SNew(SBox).HeightOverride(2.f)[SNew(SBorder).BorderImage(&AimLine).Padding(0)]]
                + SOverlay::Slot().HAlign(HAlign_Center)
                [SNew(SBox).WidthOverride(2.f)[SNew(SBorder).BorderImage(&AimLine).Padding(0)]]
            ]
        ]
        + SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Bottom).Padding(12, 0, 12, 14)
        [
            SNew(SVerticalBox)
            .Visibility(EVisibility::SelfHitTestInvisible)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
            [
                SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
                .Visibility_Lambda([Field]
                {
                    return ShowPlaybackControls(Field) ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
                })
                [
                    SNew(SBorder)
                    .BorderImage(&Panel)
                    .Visibility(EVisibility::SelfHitTestInvisible)
                    .Padding(FMargin(8.f))
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
                        [
                            MakeControl(TAttribute<FText>::CreateLambda([Field]
                            {
                                const auto* Battle = Field.Get();
                                return Battle && Battle->IsBattlePaused()
                                    ? LOCTEXT("ResumeAction", "Продолжить") : LOCTEXT("PauseAction", "Пауза");
                            }), LOCTEXT("PauseHelp", "Остановить или продолжить бой"), 142.f, FSlateColor(ControlTint),
                            FOnClicked::CreateLambda([Field]
                            {
                                if (auto* Battle = Field.Get())
                                {
                                    Battle->ToggleBattlePause();
                                    PlayMenuSound(Battle->GetWorld(), EMenuSound::Click);
                                }
                                return FReply::Handled();
                            }), TAttribute<bool>::CreateLambda([Field]
                            {
                                const auto* Battle = Field.Get();
                                return Battle && (Battle->GetHudState().Status == ERegimentBattleStatus::Running ||
                                                  Battle->GetHudState().Status == ERegimentBattleStatus::FormingUp);
                            }))
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 3, 0)
                        [
                            MakeSpeedControl(Field, ERegimentBattleSpeed::Half, LOCTEXT("Half", "×0,5"),
                                LOCTEXT("HalfHelp", "Половинная скорость"))
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 3, 0)
                        [
                            MakeSpeedControl(Field, ERegimentBattleSpeed::Normal, LOCTEXT("Normal", "×1"),
                                LOCTEXT("NormalHelp", "Обычная скорость"))
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
                        [
                            MakeSpeedControl(Field, ERegimentBattleSpeed::Double, LOCTEXT("Double", "×2"),
                                LOCTEXT("DoubleHelp", "Двойная скорость"))
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
                        [
                            MakeControl(LOCTEXT("Restart", "Сначала"), LOCTEXT("RestartHelp", "Вернуть полки на исходные позиции боя"),
                                114.f, FSlateColor(ControlTint), FOnClicked::CreateLambda([Field]
                                {
                                    if (auto* Battle = Field.Get())
                                    {
                                        PlayMenuSound(Battle->GetWorld(), EMenuSound::Confirm);
                                        Battle->RequestBattleRestart();
                                    }
                                    return FReply::Handled();
                                }))
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
                        [
                            SNew(SBox)
                            .Visibility_Lambda([Field]
                            {
                                const auto* Battle = Field.Get();
                                return Battle && !Battle->GetMissionReport().IsEmpty()
                                    ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
                            })
                            [
                                MakeControl(LOCTEXT("ReleaseReserve", "Ввести резерв"),
                                    LOCTEXT("ReleaseReserveHelp", "Отдать приказ второму полку на вступление в бой"),
                                    152.f, FSlateColor(SelectedTint), FOnClicked::CreateLambda([Field]
                                    {
                                        if (auto* Battle = Field.Get())
                                            Battle->IntroduceReserve();
                                        return FReply::Handled();
                                    }), TAttribute<bool>::CreateLambda([Field]
                                    {
                                        const auto* Battle = Field.Get();
                                        return Battle && Battle->CanIntroduceReserve();
                                    }))
                            ]
                        ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 4, 0)
                        [
                            SNew(SBox)
                            .Visibility_Lambda([Field]
                            {
                                const auto* Battle = Field.Get();
                                return Battle && Battle->CanReturnToMissionSetup()
                                    ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
                            })
                            [
                                MakeControl(LOCTEXT("MissionSetup", "К сценарию"),
                                    LOCTEXT("MissionSetupHelp", "Вернуться к выбору и настройке пресета"),
                                    124.f, FSlateColor(ControlTint), FOnClicked::CreateLambda([Field]
                                    {
                                        if (auto* Battle = Field.Get())
                                        {
                                            PlayMenuSound(Battle->GetWorld(), EMenuSound::Back);
                                            Battle->RequestMissionSetup();
                                        }
                                        return FReply::Handled();
                                    }))
                            ]
                        ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [
                            SNew(SBox).HeightOverride(36.f)
                            [RegimentAudioSettings::MakeSettingsButton(World, &ControlStyle(), ControlTint, true)]
                        ]
                    ]
                ]
            ]
        ]
        ]
        + SOverlay::Slot()[SNew(SRegimentOfficerPicker).Field(Field)]
    ];
}

#undef LOCTEXT_NAMESPACE
