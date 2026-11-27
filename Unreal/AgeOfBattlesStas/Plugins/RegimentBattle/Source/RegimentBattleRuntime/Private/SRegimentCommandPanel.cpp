#include "SRegimentCommandPanel.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "RegimentBattleField.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RegimentCommandPanel"

namespace
{
const FLinearColor CommandInk(FColor::FromHex(TEXT("F7F3EB")));
const FLinearColor CommandMuted(FColor::FromHex(TEXT("BDC8D0")));
const FLinearColor CommandGold(FColor::FromHex(TEXT("F0C98E")));
const FSlateRoundedBoxBrush CommandBackground(FLinearColor(FColor::FromHex(TEXT("101725FA"))),
    10.f, FLinearColor(FColor::FromHex(TEXT("4A5A65"))), 1.f);
const FSlateRoundedBoxBrush BranchBackground(FLinearColor(FColor::FromHex(TEXT("202D40EF"))), 4.f);
const FSlateRoundedBoxBrush LabelBackground(FLinearColor(FColor::FromHex(TEXT("101923E8"))), 4.f);
const FSlateRoundedBoxBrush GeneralLabelBackground(FLinearColor(FColor::FromHex(TEXT("101923E8"))),
    4.f, CommandGold, 1.f);

FLinearColor TeamText(int32 Team)
{
    return FLinearColor(FColor::FromHex(Team == 0 ? TEXT("84CDFA") : TEXT("FDB589")));
}

FText OrderCaption(const FRegimentDecisionHudEntry& Entry)
{
    return Entry.bPreferOrderState || Entry.Action.IsEmpty() ? Entry.State : Entry.Action;
}

FText ContactCaption(const FRegimentDecisionHudEntry& Entry)
{
    if (Entry.MeleeRegiments == 0)
        return LOCTEXT("NoMelee", "Вне рукопашной");
    return Entry.Regiment ? LOCTEXT("InMelee", "В рукопашной")
        : FText::Format(LOCTEXT("MeleeRegiments", "Полков в рукопашной · {0}"), Entry.MeleeRegiments);
}
}

const FRegimentDecisionHudEntry* SRegimentCommandPanel::Decision(uint64 Node) const
{
    if (const auto* Battle = Field.Get())
        for (const auto& Entry : Battle->GetDecisionEntries())
            if (Entry.Node == Node)
                return &Entry;
    return nullptr;
}

TSharedRef<SWidget> SRegimentCommandPanel::MakeBranch(const FRegimentDecisionHudEntry& Entry, int32 Depth)
{
    const uint64 Node = Entry.Node;
    auto Content = SNew(SVerticalBox);
    if (Entry.Team == 0)
        Content->AddSlot().AutoHeight().Padding(6, 3, 6, 8)
        [
            SNew(SButton)
            .IsFocusable(false)
            .ContentPadding(FMargin(6, 4))
            .ToolTipText(LOCTEXT("PossessHelp", "Взять под управление этого офицера"))
            .Visibility_Lambda([this, Node]
            {
                const auto* Battle = Field.Get();
                return Battle && Battle->CanPossessOfficer(Node)
                    ? EVisibility::Visible : EVisibility::Collapsed;
            })
            .OnClicked_Lambda([this, Node]
            {
                if (auto* Battle = Field.Get())
                    Battle->OpenOfficerPicker(Node);
                return FReply::Handled();
            })
            [
                SNew(STextBlock)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                .Text(LOCTEXT("Possess", "Вселиться"))
            ]
        ];
    if (Entry.Regiment && Entry.Team == 0)
        Content->AddSlot().AutoHeight().Padding(6, 0, 6, 9)
        [
            SNew(SButton)
            .IsFocusable(false)
            .ContentPadding(FMargin(6, 4))
            .ToolTipText(LOCTEXT("RallyHelp", "Полк отходит в точку сбора и восстанавливает строй в безопасности."))
            .Visibility_Lambda([this, Node]
            {
                const auto* Current = Decision(Node);
                return Current && Current->bCanRally ? EVisibility::Visible : EVisibility::Collapsed;
            })
            .OnClicked_Lambda([this, Node]
            {
                if (auto* Battle = Field.Get())
                    Battle->RallyRegiment(Node);
                return FReply::Handled();
            })
            [
                SNew(STextBlock)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                .Text(LOCTEXT("RallyRegiment", "Собрать полк"))
            ]
        ];
    if (Entry.Regiment && Entry.Team == 0)
        Content->AddSlot().AutoHeight().Padding(6, 0, 6, 9)
        [
            SNew(SButton)
            .IsFocusable(false)
            .ContentPadding(FMargin(6, 4))
            .ToolTipText(LOCTEXT("SkirmisherHelp", "Офицер выводит лучников для прикрытия полка или отзывает их в строй."))
            .Visibility_Lambda([this, Node]
            {
                const auto* Current = Decision(Node);
                return Current && Current->bCanDetach ? EVisibility::Visible : EVisibility::Collapsed;
            })
            .IsEnabled_Lambda([this, Node]
            {
                const auto* Current = Decision(Node);
                return Current && !Current->bSkirmisherOrderQueued;
            })
            .OnClicked_Lambda([this, Node]
            {
                if (auto* Battle = Field.Get())
                    Battle->ToggleSkirmishers(Node);
                return FReply::Handled();
            })
            [
                SNew(STextBlock)
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
                .Text_Lambda([this, Node]
                {
                    const auto* Current = Decision(Node);
                    if (!Current)
                        return FText::GetEmpty();
                    if (Current->bSkirmisherOrderQueued)
                        return LOCTEXT("SkirmishersQueued", "Приказ ожидает исполнения");
                    return Current->bSkirmishersEnabled
                        ? FText::Format(LOCTEXT("RecallSkirmishers", "Отозвать застрельщиков ({0})"), Current->Skirmishers)
                        : LOCTEXT("DeploySkirmishers", "Выслать застрельщиков");
                })
            ]
        ];

    Content->AddSlot().AutoHeight().Padding(6, 3, 6, 9)
    [
        SNew(STextBlock)
        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 15))
        .ColorAndOpacity(CommandMuted)
        .AutoWrapText(true)
        .Text_Lambda([this, Node]
        {
            const auto* Current = Decision(Node);
            if (!Current)
                return FText::GetEmpty();
            return Current->Contact.IsEmpty() || (Current->MeleeRegiments && !Current->Regiment) ? Current->Detail
                : FText::Format(LOCTEXT("ExpandedDecision", "{0}\n{1}"), Current->Contact, Current->Detail);
        })
    ];
    Content->AddSlot().AutoHeight().Padding(6, 0, 6, 9)
    [
        SNew(STextBlock)
        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
        .ColorAndOpacity(CommandGold)
        .AutoWrapText(true)
        .Text_Lambda([this, Node]
        {
            const auto* Current = Decision(Node);
            return Current ? Current->Intel : FText::GetEmpty();
        })
    ];

    if (const auto* Battle = Field.Get())
        for (const auto& Child : Battle->GetDecisionEntries())
            if (Child.Parent == Node)
                Content->AddSlot().AutoHeight().Padding(8, 1, 0, 3)
                [
                    MakeBranch(Child, Depth + 1)
                ];

    return SNew(SExpandableArea)
        .InitiallyCollapsed(true)
        .AllowAnimatedTransition(false)
        .BorderImage(&BranchBackground)
        .Padding(FMargin(4, 0, 4, 2))
        .HeaderPadding(FMargin(4, 5))
        .OnAreaExpansionChanged_Lambda([this, Node](bool bExpanded)
        {
            if (bExpanded)
                FocusedNode = Node;
            else
                for (auto* Current = Decision(FocusedNode); Current; Current = Decision(Current->Parent))
                    if (Current->Node == Node)
                    {
                        FocusedNode = 0;
                        break;
                    }
        })
        .HeaderContent()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(STextBlock)
                .ColorAndOpacity(TeamText(Entry.Team))
                .Font(FCoreStyle::GetDefaultFontStyle("Bold", Depth == 0 ? 17 : 16))
                .Text(Entry.Name)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
            [
                SNew(STextBlock)
                .ColorAndOpacity(CommandInk)
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
                .AutoWrapText(true)
                .Text_Lambda([this, Node]
                {
                    const auto* Current = Decision(Node);
                    return Current ? OrderCaption(*Current) : FText::GetEmpty();
                })
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 1, 0, 0)
            [
                SNew(STextBlock)
                .ColorAndOpacity_Lambda([this, Node]() -> FSlateColor
                {
                    const auto* Current = Decision(Node);
                    return Current && Current->MeleeRegiments > 0 ? CommandGold : CommandMuted;
                })
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
                .AutoWrapText(true)
                .Text_Lambda([this, Node]
                {
                    const auto* Current = Decision(Node);
                    return Current ? ContactCaption(*Current) : FText::GetEmpty();
                })
            ]
        ]
        .BodyContent()
        [
            SNew(SBorder).BorderImage(&BranchBackground).Padding(FMargin(0))[Content]
        ];
}

void SRegimentCommandPanel::Construct(const FArguments& InArgs)
{
    Field = InArgs._Field;
    SetVisibility(TAttribute<EVisibility>::CreateLambda([this]
    {
        const auto* Battle = Field.Get();
        return Battle && Battle->GetOfficerControlState().bActive
            ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible;
    }));

    auto Markers = SNew(SConstraintCanvas);
    auto Branches = SNew(SVerticalBox);
    if (const auto* Battle = Field.Get())
    {
        for (const auto& Entry : Battle->GetDecisionEntries())
        {
            const uint64 Node = Entry.Node;
            if (!Entry.Parent)
                Branches->AddSlot().AutoHeight().Padding(0, 0, 5, 6)[MakeBranch(Entry, 0)];

            Markers->AddSlot()
                .AutoSize(true)
                .Alignment(FVector2D(.5, 1))
                .Offset(TAttribute<FMargin>::CreateLambda([this, Node]
                {
                    const auto* Location = VisibleHints.Find(Node);
                    return Location ? FMargin(Location->X, Location->Y, 0, 0) : FMargin(0);
                }))
                [
                    SNew(SBorder)
                    .BorderImage(Entry.bGeneral ? &GeneralLabelBackground : &LabelBackground)
                    .Padding(FMargin(7, 3))
                    .Visibility_Lambda([this, Node]
                    {
                        return VisibleHints.Contains(Node) ? EVisibility::Visible : EVisibility::Collapsed;
                    })
                    .ToolTipText_Lambda([this, Node]
                    {
                        const auto* Current = Decision(Node);
                        return Current ? (Current->Contact.IsEmpty() ? Current->Intel : Current->Contact) : FText::GetEmpty();
                    })
                    [
                        SNew(STextBlock)
                        .ColorAndOpacity(TeamText(Entry.Team))
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", Entry.bGeneral ? 13 : 11))
                        .Text_Lambda([this, Node]
                        {
                            if (const auto* Current = Decision(Node))
                                return FText::Format(LOCTEXT("WorldDecision", "{0} · {1}"), Current->Name,
                                    Current->MeleeRegiments && Current->Regiment
                                        ? Current->Contact : Current->State);
                            return FText::GetEmpty();
                        })
                    ]
                ];
        }
    }

    ChildSlot
    [
        SNew(SOverlay)
        .Visibility(EVisibility::SelfHitTestInvisible)
        + SOverlay::Slot()[Markers]
        + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0, 60, 18, 0)
        [
            SNew(SBox).WidthOverride(326.f)
            [
                SNew(SExpandableArea)
                .InitiallyCollapsed(true)
                .AllowAnimatedTransition(false)
                .AreaTitle(LOCTEXT("OfficerDecisions", "Решения офицеров"))
                .AreaTitleFont(FCoreStyle::GetDefaultFontStyle("Bold", 17))
                .BorderImage(&CommandBackground)
                .HeaderPadding(FMargin(10, 9))
                .Padding(FMargin(9, 2, 6, 8))
                .OnAreaExpansionChanged_Lambda([this](bool bExpanded)
                {
                    if (!bExpanded)
                        FocusedNode = 0;
                })
                .BodyContent()
                [
                    SNew(SBorder).BorderImage(&CommandBackground).Padding(FMargin(6))
                    [
                        SNew(SBox).MaxDesiredHeight(335.f)
                        [
                            SNew(SScrollBox)
                            + SScrollBox::Slot()[Branches]
                        ]
                    ]
                ]
            ]
        ]
    ];
}

void SRegimentCommandPanel::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    VisibleHints.Reset();
    const auto* Battle = Field.Get();
    if (!Battle)
        return;

    const FVector2D Size = Geometry.GetLocalSize();
    for (const auto& Entry : Battle->GetDecisionEntries())
    {
        if (FocusedNode != 0 ? Entry.Node != FocusedNode : Entry.Parent != 0)
            continue;
        FVector2D Normalized;
        if (!Battle->ProjectCommander(Entry.Commander, Normalized))
            continue;
        FVector2D Position(Normalized.X * Size.X, Normalized.Y * Size.Y);
        if (Position.Y < 110 || Position.Y > Size.Y - 76)
            continue;
        Position.X = FMath::Clamp(Position.X, 140.0, FMath::Max(140.0, Size.X - 140.0));
        bool bOverlap = false;
        for (const auto& Existing : VisibleHints)
            bOverlap |= FMath::Abs(Existing.Value.X - Position.X) < 230.0 &&
                FMath::Abs(Existing.Value.Y - Position.Y) < 29.0;
        if (!bOverlap)
            VisibleHints.Add(Entry.Node, Position);
    }
}

#undef LOCTEXT_NAMESPACE
