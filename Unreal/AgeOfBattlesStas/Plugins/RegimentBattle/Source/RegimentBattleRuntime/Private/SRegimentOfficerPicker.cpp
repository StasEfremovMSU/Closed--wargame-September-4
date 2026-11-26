#include "SRegimentOfficerPicker.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "RegimentBattleField.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RegimentOfficerPicker"

namespace
{
const FSlateRoundedBoxBrush PickerPanel(FLinearColor(.043f, .061f, .074f, .96f), 7.f);
const FSlateRoundedBoxBrush PickerShade(FLinearColor(.015f, .024f, .036f, .26f), 0.f);
const FLinearColor PickerGold(.96f, .76f, .48f);
constexpr float BubbleWidth = 225.f;
constexpr float BubbleHeight = 42.f;
}

void SRegimentOfficerPicker::Construct(const FArguments& Args)
{
    Field = Args._Field;
    SetVisibility(TAttribute<EVisibility>::CreateLambda([this]
    {
        return Field.IsValid() && Field->IsChoosingOfficer() ? EVisibility::Visible : EVisibility::Collapsed;
    }));
    ChildSlot
    [
        SNew(SOverlay)
        + SOverlay::Slot()[SNew(SBorder).BorderImage(&PickerShade)]
        + SOverlay::Slot()[SAssignNew(Bubbles, SConstraintCanvas)]
        + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(12, 8)
        [
            SNew(SBox).WidthOverride(300.f).MaxDesiredHeight(475.f)
            [
                SNew(SBorder).BorderImage(&PickerPanel).Padding(8)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(2, 0, 0, 6)
                    [
                        SNew(STextBlock).Text(LOCTEXT("ChooseOfficer", "Пауза · выберите офицера"))
                        .ColorAndOpacity(PickerGold).Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
                    ]
                    + SVerticalBox::Slot().FillHeight(1.f)[SAssignNew(Roster, SScrollBox)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
                    [
                        SNew(SButton).IsFocusable(false).HAlign(HAlign_Center)
                        .OnClicked_Lambda([this]
                        {
                            if (auto* Battle = Field.Get()) Battle->CloseOfficerPicker();
                            return FReply::Handled();
                        })
                        [SNew(STextBlock).Text(LOCTEXT("Cancel", "Esc · отменить"))]
                    ]
                ]
            ]
        ]
    ];
}

TSharedRef<SWidget> SRegimentOfficerPicker::Choice(const FRegimentDecisionHudEntry& Entry, bool bCompact)
{
    const uint64 Node = Entry.Node;
    auto Row = SNew(SHorizontalBox);
    if (UTexture2D* Portrait = Entry.Portrait.Get())
    {
        auto Brush = MakeShared<FSlateBrush>();
        Brush->SetResourceObject(Portrait);
        Brushes.Add(Brush);
        const float Size = bCompact ? 22.f : 34.f;
        Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
        [
            SNew(SBox).WidthOverride(Size).HeightOverride(Size)[SNew(SImage).Image(&Brush.Get())]
        ];
    }
    else
    {
        Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(3, 0, 7, 0)
        [
            SNew(STextBlock).Text(FText::FromString(Entry.RankIcon)).ColorAndOpacity(PickerGold)
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", bCompact ? 13 : 19))
        ];
    }
    auto Text = SNew(SVerticalBox);
    Text->AddSlot().AutoHeight()
    [
        SNew(STextBlock).Text(Entry.Duty)
        .Font(FCoreStyle::GetDefaultFontStyle("Bold", bCompact ? 11 : 12))
        .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
    ];
    Text->AddSlot().AutoHeight()
    [
        SNew(STextBlock).Text(Entry.CommanderName)
        .Font(FCoreStyle::GetDefaultFontStyle("Regular", bCompact ? 10 : 11))
        .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
    ];
    Row->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)[Text];
    const FText Details = FText::Format(LOCTEXT("ChoiceDetails", "{0} · {1}\n{2}\n{3}"),
        Entry.Duty, Entry.CommanderName, Entry.Name, Entry.Effects);
    return SNew(SButton).IsFocusable(false).HAlign(HAlign_Fill).ContentPadding(FMargin(5, 3))
        .ToolTipText(Details)
        .ButtonColorAndOpacity(Field->GetPreferredOfficerNode() == Node
            ? FLinearColor(.57f, .38f, .17f) : Entry.CommandTier == 3 ? FLinearColor(.34f, .25f, .16f)
            : FLinearColor(.16f, .22f, .27f))
        .OnClicked_Lambda([this, Node]
        {
            if (auto* Battle = Field.Get()) Battle->ChooseOfficer(Node);
            return FReply::Handled();
        })[Row];
}

void SRegimentOfficerPicker::Rebuild(ARegimentBattleField& Battle)
{
    Bubbles->ClearChildren();
    Roster->ClearChildren();
    Brushes.Reset();
    for (int32 Tier = 3; Tier >= 1; --Tier)
    {
        for (const auto& Entry : Battle.GetDecisionEntries())
        {
            if (Entry.CommandTier != Tier || !Entry.Commander || !Battle.CanPossessOfficer(Entry.Node))
                continue;
            const uint64 Node = Entry.Node;
            Roster->AddSlot().Padding(0, 1)[Choice(Entry, false)];
            Bubbles->AddSlot().Alignment(FVector2D(.5, 1))
            .Offset_Lambda([this, Node]
            {
                const auto* Pos = Positions.Find(Node);
                return Pos ? FMargin(Pos->X, Pos->Y, BubbleWidth, BubbleHeight)
                    : FMargin(-1000, -1000, BubbleWidth, BubbleHeight);
            })
            [Choice(Entry, true)];
        }
    }
}

void SRegimentOfficerPicker::Tick(const FGeometry& Geometry, double Now, float Delta)
{
    SCompoundWidget::Tick(Geometry, Now, Delta);
    auto* Battle = Field.Get();
    if (!Battle || !Battle->IsChoosingOfficer()) return;
    if (Revision != Battle->GetOfficerPickerRevision())
    {
        Rebuild(*Battle);
        Revision = Battle->GetOfficerPickerRevision();
    }
    Positions.Reset();
    const auto Size = Geometry.GetLocalSize();
    for (const auto& Entry : Battle->GetDecisionEntries())
    {
        FVector2D Normalized;
        if (!Entry.Commander || !Battle->ProjectCommander(Entry.Commander, Normalized))
            continue;
        FVector2D Position = Normalized * FVector2D(Size);
        Position.X = FMath::Clamp(Position.X, double(BubbleWidth / 2), FMath::Max(double(BubbleWidth / 2), double(Size.X - BubbleWidth / 2)));
        // Stack nearby labels above their officers. The roster remains available for offscreen figures.
        bool bMoved;
        do
        {
            bMoved = false;
            for (const auto& Prior : Positions)
            {
                if (FMath::Abs(Prior.Value.X - Position.X) < BubbleWidth + 2 &&
                    FMath::Abs(Prior.Value.Y - Position.Y) < BubbleHeight + 2)
                {
                    Position.Y = Prior.Value.Y - BubbleHeight - 3;
                    bMoved = true;
                    break;
                }
            }
        } while (bMoved);
        if (Position.Y >= BubbleHeight)
            Positions.Add(Entry.Node, Position);
    }
}

#undef LOCTEXT_NAMESPACE
