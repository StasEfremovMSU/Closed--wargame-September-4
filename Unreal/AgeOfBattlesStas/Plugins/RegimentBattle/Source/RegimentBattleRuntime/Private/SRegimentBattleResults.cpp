#include "SRegimentBattleResults.h"
#include "RegimentBattlePlayback.h"
#include "RegimentBattleReport.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RegimentBattleResults"

namespace
{
const FLinearColor Ink(FColor::FromHex(TEXT("F7F3EB")));
const FLinearColor Muted(FColor::FromHex(TEXT("B9C4CC")));
const FLinearColor Brass(FColor::FromHex(TEXT("F0C98E")));
const FLinearColor Blue(FColor::FromHex(TEXT("84CDFA")));
const FLinearColor Orange(FColor::FromHex(TEXT("FDB589")));
const FSlateColorBrush Dim(FLinearColor(.008f, .013f, .02f, .75f));
const FSlateRoundedBoxBrush Panel(FLinearColor(FColor::FromHex(TEXT("131E29"))), 12.f,
    FLinearColor(FColor::FromHex(TEXT("475562"))), 1.f);
const FSlateRoundedBoxBrush RowTint(FLinearColor(FColor::FromHex(TEXT("1E2C38"))), 4.f);
const FSlateRoundedBoxBrush TotalTint(FLinearColor(FColor::FromHex(TEXT("2D4051"))), 4.f);

TSharedRef<STextBlock> Text(const FText& Label, int32 Size = 15, const FLinearColor& Color = Ink,
    bool bBold = false, bool bWrap = false)
{
    return SNew(STextBlock).Text(Label).ColorAndOpacity(Color).AutoWrapText(bWrap)
        .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
        .Font(FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size));
}

FText Duration(uint64 Seconds)
{
    if (Seconds >= 3600)
        return FText::Format(LOCTEXT("HoursMinutes", "{0} ч {1} мин"),
            FText::AsNumber(Seconds / 3600), FText::AsNumber(Seconds / 60 % 60));
    return FText::Format(LOCTEXT("MinutesSeconds", "{0} мин {1} с"),
        FText::AsNumber(Seconds / 60), FText::AsNumber(Seconds % 60));
}

FText TimelineTime(battle::Tick Tick)
{
    const uint64 Seconds = Tick / FRegimentBattlePlayback::StepsPerSecond;
    return FText::AsCultureInvariant(FString::Printf(TEXT("%02llu:%02llu"), Seconds / 60, Seconds % 60));
}

TSharedRef<SWidget> Action(const FText& Label, FSimpleDelegate ActionDelegate, bool bPrimary = false)
{
    return SNew(SButton)
        .HAlign(HAlign_Center)
        .ButtonColorAndOpacity(FSlateColor(bPrimary ? FLinearColor(FColor::FromHex(TEXT("755629")))
            : FLinearColor(FColor::FromHex(TEXT("35485B")))))
        .ContentPadding(FMargin(22.f, 9.f))
        .OnClicked_Lambda([ActionDelegate]
        {
            ActionDelegate.ExecuteIfBound();
            return FReply::Handled();
        })
        [Text(Label, 16, Ink, true)];
}

class SResultsOverlay final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SResultsOverlay) {}
        SLATE_DEFAULT_SLOT(FArguments, Content)
        SLATE_EVENT(FSimpleDelegate, OnClose)
        SLATE_ARGUMENT(float, PanelWidth)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        OnClose = Args._OnClose;
        ChildSlot
        [
            SNew(SBorder).BorderImage(&Dim).Padding(18.f)
            [
                SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
                [
                    SNew(SBox).WidthOverride(Args._PanelWidth)
                    [SNew(SBorder).BorderImage(&Panel).Padding(FMargin(28.f, 22.f))[Args._Content.Widget]]
                ]
            ]
        ];
    }

    bool SupportsKeyboardFocus() const override { return true; }
    FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override
    {
        if (Event.GetKey() != EKeys::Escape)
            return SCompoundWidget::OnKeyDown(Geometry, Event);
        OnClose.ExecuteIfBound();
        return FReply::Handled();
    }

private:
    FSimpleDelegate OnClose;
};

TSharedRef<SWidget> ScoreRow(const FText& Name, const FText (&Values)[5],
    const FSlateBrush* Background, const FLinearColor& Color, bool bBold)
{
    auto Row = SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
        [Text(Name, 15, Color, bBold)];
    for (const auto& Value : Values)
        Row->AddSlot().AutoWidth().VAlign(VAlign_Center)
        [SNew(SBox).WidthOverride(94.f).HAlign(HAlign_Center)[Text(Value, 14, Color, bBold)]];
    return SNew(SBorder).BorderImage(Background).Padding(FMargin(10.f, 5.f))[Row];
}

TSharedRef<SWidget> StrengthRow(const FText& Name, const FRegimentBattleCount& Count,
    const FSlateBrush* Background, const FLinearColor& Color, bool bBold)
{
    const FText Values[] = {FText::AsNumber(Count.Starting), FText::AsNumber(Count.Living),
        FText::AsNumber(Count.Fallen()), FText::AsNumber(Count.Eliminated), FText::AsNumber(Count.Panicked)};
    return ScoreRow(Name, Values, Background, Color, bBold);
}

TSharedRef<SWidget> BattleTimeline(const FRegimentBattleReport& Report)
{
    auto Timeline = SNew(SVerticalBox);
    if (Report.Moments.IsEmpty())
        return Timeline;

    Timeline->AddSlot().AutoHeight().Padding(0, 0, 0, 5.f)
    [Text(LOCTEXT("BattleMoments", "Ключевые события"), 16, Brass, true)];
    for (const auto& Moment : Report.Moments)
    {
        const bool bOutcome = Moment.Kind == ERegimentBattleMomentKind::BattleEnded;
        const FText& Description = bOutcome && !Report.ObjectiveResult.IsEmpty()
            ? Report.ObjectiveResult : Moment.Description;
        Timeline->AddSlot().AutoHeight().Padding(0, 0, 0, 2.f)
        [
            SNew(SBorder).BorderImage(&RowTint).Padding(FMargin(10.f, 4.f))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [SNew(SBox).WidthOverride(64.f)[Text(TimelineTime(Moment.Tick), 14, Muted)]]
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                [Text(Description, 14, Ink, bOutcome, true)]
            ]
        ];
    }
    return Timeline;
}

TSharedRef<SWidget> Statistic(const FText& Label, const FText& Value)
{
    return SNew(SBorder).BorderImage(&RowTint).Padding(FMargin(15.f, 10.f))
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[Text(Label, 14, Muted, false, true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)[Text(Value, 24, Ink, true)]
    ];
}
}

TSharedRef<SWidget> MakeRegimentBattleDebrief(const FRegimentBattleReport& Report,
    FSimpleDelegate OnRestart, FSimpleDelegate OnLeave)
{
    const bool bDraw = Report.Outcome == battle::BattleOutcome::Draw;
    const int32 Winner = Report.Winner == 1 ? 1 : 0;
    const FText Result = bDraw ? LOCTEXT("Draw", "Ничья") : FText::Format(LOCTEXT("Winner", "Победа · {0}"),
        Report.SideNames[Winner]);
    const FText Headings[] = {LOCTEXT("Started", "Вышло"), LOCTEXT("Alive", "Уцелело"), LOCTEXT("Killed", "Погибли"),
        LOCTEXT("Eliminations", "Убили"), LOCTEXT("Panic", "В панике")};
    auto Score = SNew(SScrollBox);
    for (int32 Team = 0; Team < 2; ++Team)
    {
        Score->AddSlot().Padding(0, Team == 0 ? 0.f : 12.f, 10.f, 3.f)
        [StrengthRow(Report.SideNames[Team], Report.Sides[Team], &TotalTint, Team == 0 ? Blue : Orange, true)];
        for (const auto& Row : Report.Rows)
            if (Row.Team == Team && Row.Count.Starting > 0)
                Score->AddSlot().Padding(0, 0, 10.f, 2.f)
                [StrengthRow(Row.Name, Row.Count, &RowTint, Ink, false)];
    }

    return SNew(SResultsOverlay).PanelWidth(940.f).OnClose(OnLeave)
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
        [Text(LOCTEXT("BattleReport", "Итоги боя"), 30, Brass, true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 9)
        [Text(Report.Mission, 18, Ink, true)]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f)[Text(Result, 19, bDraw ? Brass : (Winner == 0 ? Blue : Orange), true)]
            + SHorizontalBox::Slot().AutoWidth()[Text(Duration(Report.ElapsedSeconds), 16, Muted)]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 12)
        [BattleTimeline(Report)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 10.f, 2.f)
        [ScoreRow(LOCTEXT("Formation", "Подразделение"), Headings, &RowTint, Muted, true)]
        + SVerticalBox::Slot().AutoHeight()
        [SNew(SBox).MaxDesiredHeight(300.f)[Score]]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0, 15, 0, 0)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[Action(LOCTEXT("PlayAgain", "Сыграть снова"), OnRestart)]
            + SHorizontalBox::Slot().AutoWidth()[Action(LOCTEXT("BackToScenario", "К сценарию"), OnLeave, true)]
        ]
    ];
}

TSharedRef<SWidget> MakeRegimentPlayerStatistics(const FRegimentPlayerStats& Stats,
    bool bSavePending, FSimpleDelegate OnBack)
{
    return SNew(SResultsOverlay).PanelWidth(698.f).OnClose(OnBack)
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
        [Text(LOCTEXT("PlayerStatistics", "Статистика игры"), 30, Brass, true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 8, 0)
            [Statistic(LOCTEXT("Completed", "Завершено боёв"), FText::AsNumber(Stats.Completed()))]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 8, 0)
            [Statistic(LOCTEXT("Wins", "Победы"), FText::AsNumber(Stats.Victories))]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 8, 0)
            [Statistic(LOCTEXT("Defeats", "Поражения"), FText::AsNumber(Stats.Defeats))]
            + SHorizontalBox::Slot().FillWidth(1.f)[Statistic(LOCTEXT("Draws", "Ничьи"), FText::AsNumber(Stats.Draws))]
        ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 8, 0)
            [Statistic(LOCTEXT("Time", "Время в боях"), Duration(Stats.ElapsedSeconds))]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 8, 0)
            [Statistic(LOCTEXT("FriendlyLosses", "Потери вашей стороны"), FText::AsNumber(Stats.Fallen))]
            + SHorizontalBox::Slot().FillWidth(1.f)
            [Statistic(LOCTEXT("EnemyLosses", "Уничтожено противников"), FText::AsNumber(Stats.Eliminated))]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
        [Text(bSavePending ? LOCTEXT("SavePending", "Не удалось записать статистику. Игра повторит сохранение.")
            : LOCTEXT("StatsScope", "Итоги завершённых боёв на этом устройстве."), 14, Muted)]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(0, 12, 0, 0)
        [Action(LOCTEXT("Back", "Назад"), OnBack, true)]
    ];
}

#undef LOCTEXT_NAMESPACE
