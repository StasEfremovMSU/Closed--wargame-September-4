#include "SRegimentOfficerPicker.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "RegimentBattleField.h"
#include "SRegimentCommanderAvatar.h"
#include "Styling/CoreStyle.h"
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
const FLinearColor PickerInk(FColor::FromHex(TEXT("F7F3EB")));
const FLinearColor PickerMuted(FColor::FromHex(TEXT("BDC8D0")));
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
            SNew(SBox).WidthOverride(439.f).MaxDesiredHeight(680.f)
            [
                SNew(SBorder).BorderImage(&PickerPanel).Padding(12)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
                    [
                        SNew(STextBlock).Text(LOCTEXT("ChooseOfficer", "Пауза · выберите офицера"))
                        .ColorAndOpacity(PickerGold).Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
                        [
                            SNew(SRegimentCommanderAvatar).Size(76.f)
                            .Portrait_Lambda([this]
                            {
                                const auto* Entry = SelectedEntry();
                                return Entry ? Entry->Portrait : TWeakObjectPtr<UTexture2D>{};
                            })
                            .Rank_Lambda([this]
                            {
                                const auto* Entry = SelectedEntry();
                                return Entry ? FText::FromString(Entry->RankIcon) : FText::GetEmpty();
                            })
                        ]
                        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                        [
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight()
                            [
                                SNew(STextBlock).Text_Lambda([this]
                                {
                                    const auto* Entry = SelectedEntry();
                                    return Entry ? Entry->CommanderName : FText::GetEmpty();
                                }).AutoWrapText(true)
                                .ColorAndOpacity(PickerInk).Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
                            ]
                            + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
                            [
                                SNew(STextBlock).Text_Lambda([this]
                                {
                                    const auto* Entry = SelectedEntry();
                                    return Entry ? FText::Format(LOCTEXT("CommanderDuty", "{0} · {1}"),
                                        Entry->Duty, Entry->Name) : FText::GetEmpty();
                                }).AutoWrapText(true)
                                .ColorAndOpacity(PickerMuted).Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
                            ]
                        ]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 2)
                    [
                        SNew(STextBlock).Text_Lambda([this]
                        {
                            const auto* Entry = SelectedEntry();
                            return Entry ? Entry->TacticalSkill : FText::GetEmpty();
                        }).ColorAndOpacity(PickerGold).Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 10)
                    [
                        SNew(STextBlock).Text_Lambda([this]
                        {
                            const auto* Entry = SelectedEntry();
                            return Entry ? Entry->Effects : FText::GetEmpty();
                        })
                        .AutoWrapText(true).LineHeightPercentage(1.17f)
                        .ColorAndOpacity(PickerInk).Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
                    [
                        SNew(SButton).HAlign(HAlign_Center).ContentPadding(FMargin(8, 7))
                        .ButtonColorAndOpacity(FLinearColor(.54f, .36f, .16f))
                        .IsEnabled_Lambda([this] { return SelectedEntry() != nullptr; })
                        .OnClicked_Lambda([this]
                        {
                            if (auto* Battle = Field.Get())
                                Battle->ChooseOfficer(SelectedNode);
                            return FReply::Handled();
                        })
                        [
                            SNew(STextBlock).Text(LOCTEXT("Possess", "Вселиться"))
                            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))
                        ]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
                    [
                        SNew(STextBlock).Text(LOCTEXT("OfficerList", "Командиры на поле"))
                        .ColorAndOpacity(PickerMuted).Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
                    ]
                    + SVerticalBox::Slot().FillHeight(1.f)
                    [
                        SAssignNew(Roster, SScrollBox).AllowOverscroll(EAllowOverscroll::No)
                    ]
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
    Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 7, 0)
    [
        SNew(SRegimentCommanderAvatar).Size(bCompact ? 24.f : 35.f)
        .Portrait(Entry.Portrait).Rank(FText::FromString(Entry.RankIcon))
    ];
    auto Text = SNew(SVerticalBox);
    Text->AddSlot().AutoHeight()
    [
        SNew(STextBlock).Text(Entry.CommanderName).ColorAndOpacity(PickerInk)
        .Font(FCoreStyle::GetDefaultFontStyle("Bold", bCompact ? 11 : 14))
        .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
    ];
    Text->AddSlot().AutoHeight()
    [
        SNew(STextBlock).Text(Entry.Duty).ColorAndOpacity(PickerMuted)
        .Font(FCoreStyle::GetDefaultFontStyle("Regular", bCompact ? 10 : 12))
        .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
    ];
    Row->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)[Text];
    return SNew(SButton).IsFocusable(false).HAlign(HAlign_Fill).ContentPadding(FMargin(5, 3))
        .ToolTipText(Entry.Name)
        .ButtonColorAndOpacity_Lambda([this, Node]
        {
            return SelectedNode == Node ? FLinearColor(.57f, .38f, .17f) : FLinearColor(.16f, .22f, .27f);
        })
        .OnClicked_Lambda([this, Node]
        {
            SelectedNode = Node;
            return FReply::Handled();
        })[Row];
}

const FRegimentDecisionHudEntry* SRegimentOfficerPicker::SelectedEntry() const
{
    const auto* Battle = Field.Get();
    if (!Battle || !SelectedNode)
        return nullptr;
    for (const auto& Entry : Battle->GetDecisionEntries())
        if (Entry.Node == SelectedNode && Entry.Commander)
            return &Entry;
    return nullptr;
}

void SRegimentOfficerPicker::Rebuild(ARegimentBattleField& Battle)
{
    Bubbles->ClearChildren();
    Roster->ClearChildren();
    const uint64 Preferred = Battle.GetPreferredOfficerNode();
    SelectedNode = 0;
    for (int32 Tier = 3; Tier >= 1; --Tier)
    {
        for (const auto& Entry : Battle.GetDecisionEntries())
        {
            if (Entry.CommandTier != Tier || !Entry.Commander || !Battle.CanPossessOfficer(Entry.Node))
                continue;
            const uint64 Node = Entry.Node;
            if (!SelectedNode || Node == Preferred)
                SelectedNode = Node;
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
    Roster->ScrollToStart();
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
