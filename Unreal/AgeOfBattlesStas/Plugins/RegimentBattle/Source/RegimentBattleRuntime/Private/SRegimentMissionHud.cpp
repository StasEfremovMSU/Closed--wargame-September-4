#include "SRegimentMissionHud.h"
#include "RegimentAudioSettings.h"
#include "RegimentEraCatalog.h"
#include "RegimentFigureEquipment.h"
#include "RegimentGameModules.h"
#include "RegimentHistoricalCampaign.h"
#include "RegimentMissionScenario.h"
#include "SRegimentMissionDeploymentMap.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Char.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#include <algorithm>

#define LOCTEXT_NAMESPACE "RegimentMissionHud"

namespace MissionHudSlate
{
const FLinearColor Ink = FLinearColor(FColor::FromHex(TEXT("F7F3EB")));
const FLinearColor Muted = FLinearColor(FColor::FromHex(TEXT("B9C4CC")));
const FLinearColor Brass = FLinearColor(FColor::FromHex(TEXT("F0C98E")));
const FLinearColor ControlTint = FLinearColor(FColor::FromHex(TEXT("384653")));
const FLinearColor SelectedTint = FLinearColor(FColor::FromHex(TEXT("715025")));
const FLinearColor Blue = FLinearColor(FColor::FromHex(TEXT("84CDFA")));
const FLinearColor Orange = FLinearColor(FColor::FromHex(TEXT("FDB589")));
const FLinearColor Edge = FLinearColor(FColor::FromHex(TEXT("475562")));
const FLinearColor AtlasAccent = FLinearColor(FColor::FromHex(TEXT("C6DAF6")));
const FLinearColor AtlasSurface = FLinearColor(FColor::FromHex(TEXT("1B2A3B")));
const FLinearColor AtlasSelected = FLinearColor(FColor::FromHex(TEXT("38546D")));
const FSlateRoundedBoxBrush Panel(FLinearColor(FColor::FromHex(TEXT("121B24F0"))), 12.f, Edge, 1.f);
const FSlateRoundedBoxBrush RosterRow(FLinearColor(FColor::FromHex(TEXT("202D38"))), 3.f);
const FSlateRoundedBoxBrush AtlasBriefing(AtlasSurface, 9.f);
const FSlateColorBrush SetupBackdrop(FLinearColor(0.f, 0.f, 0.f, .26f));
const FSlateColorBrush ModuleBackdrop(FLinearColor(0.f, 0.f, 0.f, .09f));
const FSlateColorBrush MenuFallback(FLinearColor(FColor::FromHex(TEXT("101923"))));

const FSlateBrush* BackgroundArt(const TCHAR* Filename)
{
    const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("Battle/UI") / Filename);
    if (!FPaths::FileExists(Path))
        return &MenuFallback;
    static TMap<FString, TSharedPtr<FSlateDynamicImageBrush>> Art;
    auto& Brush = Art.FindOrAdd(Path);
    if (!Brush)
        Brush = MakeShared<FSlateDynamicImageBrush>(FName(*Path), FVector2D(1672, 941));
    return Brush.Get();
}

TSharedRef<SWidget> MakeBackground(const TCHAR* Filename, TAttribute<EVisibility> Visibility)
{
    return SNew(SScaleBox)
        .Clipping(EWidgetClipping::ClipToBounds)
        .Visibility(Visibility)
        .Stretch(EStretch::ScaleToFill)
        [
            SNew(SImage).Image(BackgroundArt(Filename))
        ];
}
const FSlateColorBrush ModuleRule(Edge);

const FSlateBrush* WaterlooAtlasArt()
{
    static const FString Path = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectContentDir() / TEXT("Battle/UI/AtlasWaterloo.png"));
    if (!FPaths::FileExists(Path))
        return &MenuFallback;
    static const auto Brush = []
    {
        auto Art = MakeShared<FSlateDynamicImageBrush>(FName(*Path), FVector2D(1086, 724));
        Art->SetUVRegion(FBox2f(FVector2f::ZeroVector, FVector2f(1.f, .5f)));
        return Art;
    }();
    return &Brush.Get();
}

constexpr float EnabledWidth = 44.f;
constexpr float NameWidth = 114.f;
constexpr float ProfileWidth = 142.f;
constexpr float CountWidth = 63.f;
constexpr float TotalWidth = 44.f;
constexpr float ColumnGap = 8.f;
constexpr float PresetControlHeight = 36.f;
constexpr float EditorWidth = 1360.f;
constexpr float DeploymentNumberWidth = 88.f;
constexpr float GroupWidth = 110.f;
constexpr float CoordinateWidth = 145.f;
constexpr float AutoPositionWidth = 122.f;

const FButtonStyle& ControlStyle()
{
    static const FButtonStyle Style = FButtonStyle(FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button"))
        .SetNormal(FSlateRoundedBoxBrush(FLinearColor::White * 0.80f, 8.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor::White, 8.f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor::White * 0.65f, 8.f))
        .SetNormalPadding(FMargin(0))
        .SetPressedPadding(FMargin(0, 1, 0, 0));
    return Style;
}

const FSpinBoxStyle& NumberStyle()
{
    static const FSpinBoxStyle Style = FSpinBoxStyle(FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox"))
        .SetBackgroundBrush(FSlateRoundedBoxBrush(FLinearColor(FColor::FromHex(TEXT("121B24"))), 3.f, Edge, 1.f))
        .SetHoveredBackgroundBrush(FSlateRoundedBoxBrush(FLinearColor(FColor::FromHex(TEXT("121B24"))), 3.f, Brass, 1.f))
        .SetActiveBackgroundBrush(FSlateRoundedBoxBrush(FLinearColor(FColor::FromHex(TEXT("121B24"))), 3.f, Brass, 1.f))
        .SetForegroundColor(FSlateColor(Ink));
    return Style;
}

const FEditableTextBoxStyle& PresetNameStyle()
{
    static const FEditableTextBoxStyle Style =
        FEditableTextBoxStyle(FCoreStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"))
        .SetBackgroundImageNormal(NumberStyle().BackgroundBrush)
        .SetBackgroundImageHovered(NumberStyle().HoveredBackgroundBrush)
        .SetBackgroundImageFocused(NumberStyle().ActiveBackgroundBrush)
        .SetBackgroundColor(FSlateColor(FLinearColor::White))
        .SetForegroundColor(FSlateColor(Ink))
        .SetFocusedForegroundColor(FSlateColor(Ink));
    return Style;
}

const FButtonStyle& ModuleButtonStyle()
{
    static const FButtonStyle Style = FButtonStyle(ControlStyle())
        .SetNormal(FSlateRoundedBoxBrush(FLinearColor(FColor::FromHex(TEXT("202D38E3"))), 10.f, Edge, 1.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(FColor::FromHex(TEXT("344653F2"))), 10.f, Brass, 1.f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(FColor::FromHex(TEXT("2E3842"))), 10.f, Brass, 1.f));
    return Style;
}

const FButtonStyle& AtlasRowStyle()
{
    static const FButtonStyle Style = FButtonStyle(ControlStyle())
        .SetNormal(FSlateRoundedBoxBrush(FLinearColor::White, 5.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor::White, 5.f, AtlasAccent, 1.f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor::White * .85f, 5.f, AtlasAccent, 1.f));
    return Style;
}

const FSearchBoxStyle& AtlasSearchStyle()
{
    static const FSearchBoxStyle Style =
        FSearchBoxStyle(FCoreStyle::Get().GetWidgetStyle<FSearchBoxStyle>("SearchBox"))
        .SetTextBoxStyle(FEditableTextBoxStyle(PresetNameStyle())
            .SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 15))
            .SetPadding(FMargin(8, 6)));
    return Style;
}

const FComboButtonStyle& FactionPickerStyle()
{
    static const FComboButtonStyle Style = FComboButtonStyle(FCoreStyle::Get().GetWidgetStyle<FComboButtonStyle>("ComboButton"))
        .SetButtonStyle(ControlStyle())
        .SetMenuBorderBrush(Panel);
    return Style;
}
TSharedRef<SWidget> MakeFactionPicker(const FText& Label, const FSlateColor& Color,
    TAttribute<FText> Value, FOnGetContent Menu, TAttribute<bool> Enabled = true)
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(STextBlock)
            .Text(Label)
            .ColorAndOpacity(Color)
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 17))
        ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SBox).WidthOverride(305.f).HeightOverride(PresetControlHeight)
            [
                SNew(SComboButton)
                .ComboButtonStyle(&FactionPickerStyle())
                .ButtonColorAndOpacity(FSlateColor(ControlTint))
                .ContentPadding(FMargin(10, 3))
                .ForegroundColor(FSlateColor(Ink))
                .IsEnabled(Enabled)
                .ToolTipText(Label)
                .OnGetMenuContent(Menu)
                .ButtonContent()
                [
                    SNew(STextBlock)
                    .Text(Value)
                    .ColorAndOpacity(FSlateColor(Ink))
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 15))
                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                ]
            ]
        ];
}

FText TerrainName(battle::MarchTerrain Terrain)
{
    using battle::MarchTerrain;
    switch (Terrain)
    {
        case MarchTerrain::River: return LOCTEXT("River", "Река");
        case MarchTerrain::OpenField: return LOCTEXT("OpenField", "Открытое поле");
        case MarchTerrain::Hill: return LOCTEXT("Hill", "Холм");
        case MarchTerrain::Waterloo: return LOCTEXT("WaterlooTerrain", "Ватерлоо");
        case MarchTerrain::Sekigahara: return LOCTEXT("SekigaharaTerrain", "Сэкигахара");
        case MarchTerrain::Agincourt: return LOCTEXT("AgincourtTerrain", "Азенкур");
        case MarchTerrain::Pavia: return LOCTEXT("PaviaTerrain", "Павия");
        case MarchTerrain::Poltava: return LOCTEXT("PoltavaTerrain", "Полтава");
        case MarchTerrain::Nagashino: return LOCTEXT("NagashinoTerrain", "Нагасино");
        case MarchTerrain::Gaugamela: return LOCTEXT("GaugamelaTerrain", "Гавгамелы");
        case MarchTerrain::Zama: return LOCTEXT("ZamaTerrain", "Зама");
        case MarchTerrain::Kalka: return LOCTEXT("KalkaTerrain", "Калка");
        case MarchTerrain::Dorylaeum: return LOCTEXT("DorylaeumTerrain", "Дорилей");
        case MarchTerrain::Grunwald: return LOCTEXT("GrunwaldTerrain", "Грюнвальд");
        case MarchTerrain::Crecy: return LOCTEXT("CrecyTerrain", "Креси");
        case MarchTerrain::Breitenfeld: return LOCTEXT("BreitenfeldTerrain", "Брейтенфельд");
        case MarchTerrain::Lutzen: return LOCTEXT("LutzenTerrain", "Лютцен");
        case MarchTerrain::Kawanakajima: return LOCTEXT("KawanakajimaTerrain", "Каванакадзима");
        case MarchTerrain::Narva: return LOCTEXT("NarvaTerrain", "Нарва");
        case MarchTerrain::Lesnaya: return LOCTEXT("LesnayaTerrain", "Лесная");
        case MarchTerrain::Austerlitz: return LOCTEXT("AusterlitzTerrain", "Аустерлиц");
        case MarchTerrain::Borodino: return LOCTEXT("BorodinoTerrain", "Бородино");
        case MarchTerrain::StoneWalls: return LOCTEXT("StoneWalls", "Каменные стены");
        case MarchTerrain::StirlingBridge: return LOCTEXT("StirlingBridge", "Стерлингский мост");
        case MarchTerrain::Cannae: return LOCTEXT("CannaeBanks", "Берег Ауфида");
        case MarchTerrain::Peipus: return LOCTEXT("PeipusShore", "Чудское озеро");
    }
    return FText::GetEmpty();
}

TSharedRef<SWidget> Readout(TAttribute<FText> Value)
{
    return SNew(STextBlock)
        .Text(Value)
        .ColorAndOpacity(Ink)
        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 17))
        .AutoWrapText(true);
}

FText GoalName(EMissionGoal Goal)
{
    switch (Goal)
    {
        case EMissionGoal::Crossing:
            return LOCTEXT("CrossingObjective", "Переправа");
        case EMissionGoal::ReserveExercise:
            return LOCTEXT("ReserveObjective", "Ввод резерва");
        case EMissionGoal::HillHold:
            return LOCTEXT("HillHoldObjective", "Удержать высоту");
        case EMissionGoal::Breakthrough:
            return LOCTEXT("BreakthroughObjective", "Прорыв пехоты");
        default:
            return LOCTEXT("EliminateObjective", "Разгром");
    }
}

FText GoalDescription(EMissionGoal Goal)
{
    switch (Goal)
    {
        case EMissionGoal::Crossing:
            return LOCTEXT("CrossingDescription", "Проведите заданное число полков через переправу за отведённое время. Для этой цели нужна карта с рекой.");
        case EMissionGoal::ReserveExercise:
            return LOCTEXT("ReserveDescription", "Введите резерв в бой, пока у передового полка осталось не меньше половины состава.");
        case EMissionGoal::HillHold:
            return FText::Format(LOCTEXT("HillHoldDescription",
                "На карте «Холм» удерживайте вершину {0} секунд подряд. Нужна хотя бы половина исходного состава одного синего полка в строю, включая командный состав. Вражеский боец в строю сбрасывает отсчёт."),
                MissionHillHoldSeconds);
        case EMissionGoal::Breakthrough:
            return LOCTEXT("BreakthroughDescription",
                "Выведите заданные пехотные полки к рубежу до истечения времени. В зоне должно быть не меньше половины исходного состава каждого полка в строю.");
        default:
            return LOCTEXT("EliminateDescription", "Разгромите полки противника.");
    }
}

FText RegimentNumber(int32 RegimentIndex)
{
    return FText::AsNumber(RegimentIndex % MissionRegimentsPerTeam + 1);
}

FText MarchTitle(EMissionHudStage Stage)
{
    switch (Stage)
    {
        case EMissionHudStage::EnemyFound:
            return LOCTEXT("EnemyFound", "Противник обнаружен");
        case EMissionHudStage::MarchWithOrders:
            return LOCTEXT("MarchWithOrders", "Поход продолжается");
        default:
            return LOCTEXT("March", "Полки на марше");
    }
}

FText MarchDetail(EMissionHudStage Stage)
{
    switch (Stage)
    {
        case EMissionHudStage::EnemyFound:
            return LOCTEXT("AwaitingWaypoint", "Пауза до назначения точки маршрута");
        case EMissionHudStage::MarchWithOrders:
            return LOCTEXT("OrdersInEffect", "Противник замечен. Меняйте маршрут.");
        default:
            return LOCTEXT("InitialMarch", "Полки следуют по начальным маршрутам.");
    }
}

TSharedRef<SWidget> MakeText(TAttribute<FText> Text, int32 Size, TAttribute<FSlateColor> Color, bool bBold = false)
{
    return SNew(STextBlock)
        .Visibility(EVisibility::HitTestInvisible)
        .Font(FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size))
        .ColorAndOpacity(Color)
        .Text(Text);
}

TSharedRef<SWidget> MakeControl(TAttribute<FText> Label, FText Tooltip, float Width, float Height,
    TAttribute<FSlateColor> Tint, FOnClicked OnClick, bool bIsFocusable)
{
    return SNew(SBox)
        .Visibility(EVisibility::SelfHitTestInvisible)
        .WidthOverride(Width)
        .HeightOverride(Height)
        [
            SNew(SButton)
            .ButtonStyle(&ControlStyle())
            .ButtonColorAndOpacity(Tint)
            .ContentPadding(FMargin(10, 3))
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            .IsFocusable(bIsFocusable)
            .ToolTipText(Tooltip)
            .OnClicked(OnClick)
            [
                MakeText(Label, 15, FSlateColor(Ink), true)
            ]
        ];
}

void ResetPlatformRow(FMissionRosterEntry& Entry, bool bCavalry)
{
    FMissionRosterEntry Conventional;
    Conventional.bEnabled = Entry.bEnabled;
    Conventional.bCavalry = bCavalry;
    Conventional.Group = Entry.Group;
    Conventional.StartX = Entry.StartX;
    Conventional.StartY = Entry.StartY;
    Entry = Conventional;
}

bool HasPlatformChoices(const FRegimentEraFaction* Faction)
{
    return Faction && std::ranges::any_of(Faction->Platforms, [](const auto& Profile) { return Profile.IsSet(); });
}

TSharedRef<SWidget> MakeNumberInput(FMissionRosterEntry& Entry, int32 FMissionRosterEntry::*Member,
    int32 Maximum, const TAttribute<FText>& Tooltip, FSimpleDelegate OnEdit)
{
    const bool bPlatform = Member == &FMissionRosterEntry::PlatformCount;
    const int32 Minimum = bPlatform ? 1 : 0;
    const auto IsEditable = [&Entry, Member, bPlatform]
    {
        if (Entry.Platform != battle::BattlePlatform::None)
            return bPlatform;
        return !bPlatform && Entry.bCavalry == (Member == &FMissionRosterEntry::Mounted);
    };
    const auto Limit = [&Entry, Member, Maximum, bPlatform]
    {
        return bPlatform ? Maximum : FMath::Min(Maximum, MissionMaxRegiment - Entry.Total() + Entry.*Member);
    };
    return SNew(SBox)
        .WidthOverride(CountWidth)
        .HeightOverride(36.f)
        [
            SNew(SSpinBox<int32>)
            .Style(&NumberStyle())
            .IsEnabled_Lambda([&Entry, IsEditable] { return Entry.bEnabled && IsEditable(); })
            .MinValue(Minimum)
            .MaxValue_Lambda([Limit]() -> TOptional<int32> { return Limit(); })
            .Delta(1)
            .EnableWheel(false)
            .BroadcastValueChangesPerKey(true)
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
            .Justification(ETextJustify::Center)
            .ContentPadding(FMargin(6.f))
            .ToolTipText_Lambda([&Entry, Tooltip]
            {
                return Entry.InfantryProfile.IsEmpty() ? Tooltip.Get()
                    : LOCTEXT("NamedInfantryCountHelp", "В выбранном типе полка эти поля задают только общую численность.");
            })
            .Value_Lambda([&Entry, Member, IsEditable, Minimum] { return IsEditable() ? Entry.*Member : Minimum; })
            .OnValueChanged_Lambda([&Entry, Member, Limit, IsEditable, OnEdit, Minimum](int32 NewValue)
            {
                if (Entry.bEnabled && IsEditable() && Entry.*Member != NewValue)
                {
                    Entry.*Member = FMath::Clamp(NewValue, Minimum, Limit());
                    OnEdit.ExecuteIfBound();
                }
            })
        ];
}

template <typename NumberType>
TSharedRef<SWidget> MakeField(float Width, TAttribute<TOptional<NumberType>> Min, TAttribute<TOptional<NumberType>> Max,
    TAttribute<FText> Tooltip,
    TAttribute<NumberType> Value, typename SSpinBox<NumberType>::FOnValueChanged OnChange,
    typename SSpinBox<NumberType>::FOnValueCommitted OnCommit = {})
{
    return SNew(SBox).WidthOverride(Width).HeightOverride(PresetControlHeight)
        [
            SNew(SSpinBox<NumberType>)
            .Style(&NumberStyle())
            .MinValue(Min)
            .MaxValue(Max)
            .Delta(NumberType(1))
            .EnableWheel(false)
            .MinFractionalDigits(0)
            .MaxFractionalDigits(2)
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
            .Justification(ETextJustify::Center)
            .ContentPadding(FMargin(6.f))
            .ToolTipText(Tooltip)
            .Value(Value)
            .OnValueChanged(OnChange)
            .OnValueCommitted(OnCommit)
        ];
}

TSharedRef<SWidget> MakeColumnHeading(TAttribute<FText> Label, float Width, EHorizontalAlignment Alignment)
{
    return SNew(SBox)
        .WidthOverride(Width)
        .HAlign(Alignment)
        [
            MakeText(Label, 14, FSlateColor(Muted), true)
        ];
}
}

void SRegimentMissionHud::Construct(const FArguments& InArgs)
{
    World = InArgs._World;
    SetVisibility(EVisibility::SelfHitTestInvisible);
    const auto MarchVisibility = [this]
    {
        return Stage == EMissionHudStage::Setup || Stage == EMissionHudStage::Modules || Stage == EMissionHudStage::Campaigns
            ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible;
    };

    ChildSlot
    [
        SNew(SOverlay)
        .Visibility(EVisibility::SelfHitTestInvisible)
        + SOverlay::Slot()
        [
            MissionHudSlate::MakeBackground(TEXT("Battlefront.png"), TAttribute<EVisibility>::CreateLambda([this]
            {
                return Stage == EMissionHudStage::Modules ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
            }))
        ]
        + SOverlay::Slot()
        [
            MissionHudSlate::MakeBackground(TEXT("CampaignTable.png"), TAttribute<EVisibility>::CreateLambda([this]
            {
                return Stage == EMissionHudStage::Setup || Stage == EMissionHudStage::Campaigns
                    ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
            }))
        ]
        + SOverlay::Slot()
        [
            SNew(SBorder)
            .Visibility_Lambda([this]
            {
                return Stage == EMissionHudStage::Modules ? EVisibility::Visible : EVisibility::Collapsed;
            })
            .BorderImage(&MissionHudSlate::ModuleBackdrop)
            .Padding(FMargin(52.f, 24.f))
            [
                SNew(SScaleBox)
                .HAlign(HAlign_Left)
                .VAlign(VAlign_Center)
                .Stretch(EStretch::ScaleToFit)
                .StretchDirection(EStretchDirection::DownOnly)
                [
                    MakeModulePanel()
                ]
            ]
        ]
        + SOverlay::Slot()
        [
            SNew(SBorder)
            .Visibility_Lambda([this]
            {
                return Stage == EMissionHudStage::Setup ? EVisibility::Visible : EVisibility::Collapsed;
            })
            .BorderImage(&MissionHudSlate::SetupBackdrop)
            .Padding(FMargin(22.f))
            [
                SNew(SScaleBox)
                .Stretch(EStretch::ScaleToFit)
                .StretchDirection(EStretchDirection::DownOnly)
                [
                    MakeSetupPanel()
                ]
            ]
        ]
        + SOverlay::Slot()
        [
            SNew(SBorder)
            .Visibility_Lambda([this]
            {
                return Stage == EMissionHudStage::Campaigns ? EVisibility::Visible : EVisibility::Collapsed;
            })
            .BorderImage(&MissionHudSlate::SetupBackdrop)
            .Padding(FMargin(22.f))
            [
                SNew(SScaleBox)
                .Stretch(EStretch::ScaleToFit)
                .StretchDirection(EStretchDirection::DownOnly)
                [MakeCampaignPanel()]
            ]
        ]
        + SOverlay::Slot().VAlign(VAlign_Top).Padding(12, 18, 12, 0)
        [
            SNew(SScaleBox)
            .Visibility_Lambda(MarchVisibility)
            .Stretch(EStretch::ScaleToFit)
            .StretchDirection(EStretchDirection::DownOnly)
            [
                MakeMarchStatus()
            ]
        ]
        + SOverlay::Slot().VAlign(VAlign_Bottom).Padding(12, 0, 12, 18)
        [
            SNew(SScaleBox)
            .Visibility_Lambda(MarchVisibility)
            .Stretch(EStretch::ScaleToFit)
            .StretchDirection(EStretchDirection::DownOnly)
            [
                MakeMarchHelp()
            ]
        ]
    ];
    RefreshBriefingSources();
}

void SRegimentMissionHud::ShowModuleMenu()
{
    SetStatus(EMissionHudStage::Modules, 0);
    if (FirstModuleButton && FSlateApplication::IsInitialized())
        FSlateApplication::Get().SetKeyboardFocus(FirstModuleButton, EFocusCause::SetDirectly);
}

void SRegimentMissionHud::ShowCampaignMenu()
{
    CampaignNotice = FText::GetEmpty();
    SetStatus(EMissionHudStage::Campaigns, 0);
}

void SRegimentMissionHud::SetModuleContext(const FString& Id, const FMissionPreset& Preset)
{
    ModuleId = Id;
    SetupStep = ESetupStep::Scenario;
    SetPreset(Preset);
    ScenarioFilter = EScenarioFilter::All;
    ScenarioQuery.Reset();
    if (ScenarioSearch)
        ScenarioSearch->SetText(FText::GetEmpty());
    RefreshPresetOptions();
    if (PresetName)
        PresetName->SetText(PendingCopyNames.FindRef(ModuleId));
    ClearPresetNotice();
    SetStatus(EMissionHudStage::Setup, 0);
}

FReply SRegimentMissionHud::SelectModule(FString Id)
{
    if (OnSelectModule)
    {
        OnSelectModule(Id);
        RegimentAudioSettings::PlayMenuSound(World, Stage == EMissionHudStage::Setup
            ? RegimentAudioSettings::EMenuSound::Confirm : RegimentAudioSettings::EMenuSound::Error);
    }
    if (Stage == EMissionHudStage::Setup && FirstScenarioButton)
        return FReply::Handled().SetUserFocus(FirstScenarioButton.ToSharedRef(), EFocusCause::SetDirectly);
    return FReply::Handled();
}

void SRegimentMissionHud::RememberDraftAndShowModules()
{
    if (Stage == EMissionHudStage::Setup && PresetName)
        PendingCopyNames.Add(ModuleId, PresetName->GetText());
    if (OnReturnToModules)
        OnReturnToModules(Draft);
}

FReply SRegimentMissionHud::SwitchScenarioEra(FString Id)
{
    if (Stage != EMissionHudStage::Setup || Id == ModuleId || !OnReturnToModules || !OnSelectModule)
        return FReply::Handled();
    RememberDraftAndShowModules();
    return SelectModule(MoveTemp(Id));
}

FReply SRegimentMissionHud::ReturnToModules()
{
    RegimentAudioSettings::PlayMenuSound(World, RegimentAudioSettings::EMenuSound::Back);
    RememberDraftAndShowModules();
    if (Stage == EMissionHudStage::Modules && FirstModuleButton)
        return FReply::Handled().SetUserFocus(FirstModuleButton.ToSharedRef(), EFocusCause::SetDirectly);
    return FReply::Handled();
}

void SRegimentMissionHud::SetTerrain(battle::MarchTerrain InTerrain)
{
    if (Draft.Terrain != InTerrain)
    {
        Draft.Terrain = InTerrain;
        ResetDeploymentPreview();
        Invalidate(EInvalidateWidgetReason::Layout);
    }
}

void SRegimentMissionHud::SetPreset(const FMissionPreset& Preset)
{
    const FString PreviousName = Draft.Name;
    Draft = Preset;
    if (Draft.Name.IsEmpty())
        Draft.Name = PreviousName;
    ResetScenarioPreview();
    ResetDeploymentPreview();
    Invalidate(EInvalidateWidgetReason::Layout);
}

void SRegimentMissionHud::ChooseTerrain(battle::MarchTerrain InTerrain)
{
    SetTerrain(InTerrain);
    if (InTerrain == battle::MarchTerrain::StirlingBridge)
        Draft.Objective.Crossing = 1;
    ClearPresetNotice();
    if (OnTerrainChanged)
        OnTerrainChanged(Draft.Terrain, Draft.Field);
}

void SRegimentMissionHud::ChooseFieldSize(battle::MarchField Field)
{
    if (Draft.Field == Field)
        return;

    Draft.Field = Field;
    ClearPresetNotice();
    Invalidate(EInvalidateWidgetReason::Layout);
}

void SRegimentMissionHud::ChooseFaction(bool bBlue, FString Id)
{
    const FRegimentEraDefinition* Era = RegimentEraCatalog::Find(FStringView(ModuleId));
    const auto* Faction = Era ? RegimentEraCatalog::FindFaction(*Era, FStringView(Id)) : nullptr;
    if (!Faction)
        return;

    FString& Selected = bBlue ? Draft.Era.BlueFaction : Draft.Era.OrangeFaction;
    if (Selected != Id)
    {
        Selected = MoveTemp(Id);
        const int32 First = bBlue ? 0 : MissionRegimentsPerTeam;
        for (int32 Index = First; Index < First + MissionRegimentsPerTeam; ++Index)
        {
            auto& Row = Draft.Roster[Index];
            if (Row.Platform != battle::BattlePlatform::None && !RegimentEraCatalog::PlatformTroop(*Faction, Row.Platform))
                MissionHudSlate::ResetPlatformRow(Row, false);
            if (!RegimentEraCatalog::MountedTroop(*Faction, Row.MountedClass))
                Row.MountedClass = ERegimentMountClass::FactionDefault;
            if (!RegimentEraCatalog::RangedTroop(*Faction, Row.RangedClass))
                Row.RangedClass = ERegimentRangedClass::FactionDefault;
            if (!Faction->InfantryProfiles.Contains(Row.InfantryProfile))
                Row.InfantryProfile.Reset();
        }
        ClearPresetNotice();
        Invalidate(EInvalidateWidgetReason::Layout);
    }
}

void SRegimentMissionHud::ClearPresetNotice()
{
    PresetStatus = FText::GetEmpty();
    bPresetError = false;
    ResetDeploymentPreview();
}

bool SRegimentMissionHud::IsCurrentScenario(const FMissionPresetListItem& Option) const
{
    return Draft.EditableCopyId.IsValid() ? Draft.EditableCopyId == Option.Id
        : Option.bBuiltIn && Draft.Name == Option.Name;
}

const FMissionPreset& SRegimentMissionHud::PreviewedPreset() const
{
    return ScenarioPreview ? ScenarioPreview.GetValue() : Draft;
}

void SRegimentMissionHud::ResetScenarioPreview()
{
    ScenarioPreview.Reset();
    PreviewId.Invalidate();
    RefreshBriefingSources();
}

void SRegimentMissionHud::RefreshPresetOptions()
{
    ScenarioOptions = RegimentGameModules::PlayablePresets(FStringView(ModuleId), RegimentMissionPresets::Directory());
    ScenarioOptions.StableSort([](const auto& Left, const auto& Right)
    {
        return Left.bSharedAcrossModules < Right.bSharedAcrossModules;
    });
    RebuildScenarioList();
}

void SRegimentMissionHud::ChangeScenarioSearch(const FText& Query)
{
    ScenarioQuery = Query.ToString().TrimStartAndEnd();
    ScenarioQuery.ReplaceInline(TEXT("ё"), TEXT("е"), ESearchCase::IgnoreCase);
    RebuildScenarioList();
}

void SRegimentMissionHud::RebuildScenarioList()
{
    using namespace MissionHudSlate;
    if (!ScenarioList)
        return;

    ScenarioList->ClearChildren();
    FirstScenarioButton.Reset();
    auto Options = ScenarioOptions.FilterByPredicate([this](const FMissionPresetListItem& Option)
    {
        if ((ScenarioFilter == EScenarioFilter::Historical && !Option.bHistorical) ||
            (ScenarioFilter == EScenarioFilter::Saved && Option.bBuiltIn))
            return false;
        if (ScenarioQuery.IsEmpty())
            return true;
        FString Name = Option.Name;
        Name.ReplaceInline(TEXT("ё"), TEXT("е"), ESearchCase::IgnoreCase);
        return Name.Contains(ScenarioQuery, ESearchCase::IgnoreCase);
    });
    int32 HistoricalCount = 0;
    int32 SavedCount = 0;
    for (const auto& Option : Options)
    {
        HistoricalCount += Option.bHistorical ? 1 : 0;
        SavedCount += Option.bBuiltIn ? 0 : 1;
    }
    ScenarioResultCount = FText::Format(LOCTEXT("ScenarioResultCount",
        "Всего {0}  ·  Исторические {1}  ·  Мои {2}"),
        FText::AsNumber(Options.Num()), FText::AsNumber(HistoricalCount), FText::AsNumber(SavedCount));

    for (const bool bBuiltIn : {true, false})
    {
        bool bAddedHeading = false;
        for (const auto& Option : Options)
        {
            if (Option.bBuiltIn != bBuiltIn)
                continue;
            if (!bAddedHeading)
            {
                ScenarioList->AddSlot().AutoHeight().Padding(0, bBuiltIn ? 0.f : 14.f, 0, 9.f)
                [
                    MakeText(bBuiltIn ? LOCTEXT("BuiltInScenarios", "Готовые сценарии")
                                      : LOCTEXT("SavedScenarios", "Мои сценарии"), 15, FSlateColor(Muted), true)
                ];
                bAddedHeading = true;
            }

            const FText Label = FText::FromString(Option.Name);
            const FText Kind = Option.bHistorical ? LOCTEXT("HistoricalKind", "Историческая") :
                Option.bPlayableExercise ? LOCTEXT("ExerciseKind", "Учебная") : FText::GetEmpty();
            auto Button = SNew(SButton)
                .ButtonStyle(&AtlasRowStyle())
                .ButtonColorAndOpacity_Lambda([this, Option]
                {
                    return FSlateColor((ScenarioPreview ? PreviewId == Option.Id : IsCurrentScenario(Option))
                        ? AtlasSelected : AtlasSurface);
                })
                .ContentPadding(FMargin(12.f, 9.f))
                .HAlign(HAlign_Fill)
                .ToolTipText(Label)
                .OnClicked(FOnClicked::CreateSP(this, &SRegimentMissionHud::PreviewScenario, Option))
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                    [
                        SNew(STextBlock).Text(Label).ColorAndOpacity(Ink).AutoWrapText(true)
                        .Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
                    ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(7, 0, 0, 0)
                    [
                        SNew(STextBlock).Text(Kind).ColorAndOpacity(AtlasAccent)
                        .Visibility(Kind.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible)
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
                    ]
                ];
            ScenarioList->AddSlot().AutoHeight().Padding(0, 0, 0, 6)[Button];
            if (!FirstScenarioButton || (ScenarioPreview ? PreviewId == Option.Id : IsCurrentScenario(Option)))
                FirstScenarioButton = Button;
        }
    }
    if (Options.IsEmpty())
    {
        ScenarioList->AddSlot().AutoHeight()
        [
            SNew(STextBlock)
            .Text(LOCTEXT("NoScenarios", "Битвы не найдены. Измените поиск или фильтр."))
            .AutoWrapText(true)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
            .ColorAndOpacity(Muted)
        ];
    }
}

FReply SRegimentMissionHud::PreviewScenario(FMissionPresetListItem Option)
{
    using RegimentAudioSettings::EMenuSound;
    using RegimentAudioSettings::PlayMenuSound;
    ClearPresetNotice();
    if (IsCurrentScenario(Option))
    {
        ResetScenarioPreview();
        PlayMenuSound(World, EMenuSound::Click);
        return FReply::Handled();
    }

    FMissionPreset Preview;
    if (!RegimentGameModules::LoadPlayable(FStringView(ModuleId), RegimentMissionPresets::Directory(), Option,
        Preview, PresetStatus))
    {
        if (PresetStatus.IsEmpty())
            PresetStatus = LOCTEXT("BuiltInNotFound", "Не удалось открыть сценарий. Выберите другой.");
        bPresetError = true;
        PlayMenuSound(World, EMenuSound::Error);
        return FReply::Handled();
    }

    ScenarioPreview = MoveTemp(Preview);
    PreviewId = Option.Id;
    RefreshBriefingSources();
    PlayMenuSound(World, EMenuSound::Click);
    return FReply::Handled();
}

FReply SRegimentMissionHud::SaveActivePreset()
{
    bPresetError = !RegimentMissionPresets::Save(RegimentMissionPresets::Directory(), Draft.EditableCopyId, Draft, PresetStatus);
    if (!bPresetError)
    {
        RefreshPresetOptions();
        PresetStatus = LOCTEXT("SavedEdits", "Изменения сохранены.");
    }
    RegimentAudioSettings::PlayMenuSound(World, bPresetError
        ? RegimentAudioSettings::EMenuSound::Error : RegimentAudioSettings::EMenuSound::Confirm);
    return FReply::Handled();
}

FReply SRegimentMissionHud::SavePresetCopy()
{
    FMissionPreset Copy = Draft;
    Copy.Name = PresetName->GetText().ToString().TrimStartAndEnd();
    FGuid SavedId;
    bPresetError = !RegimentMissionPresets::SaveCopy(RegimentMissionPresets::Directory(), Copy, SavedId, PresetStatus);
    if (!bPresetError)
    {
        Draft.EditableCopyId = SavedId;
        Draft.Name = MoveTemp(Copy.Name);
        RefreshPresetOptions();
        PresetName->SetText(FText::GetEmpty());
        PresetStatus = LOCTEXT("PresetSaved", "Копия сохранена.");
    }
    RegimentAudioSettings::PlayMenuSound(World, bPresetError
        ? RegimentAudioSettings::EMenuSound::Error : RegimentAudioSettings::EMenuSound::Confirm);
    return FReply::Handled();
}

FReply SRegimentMissionHud::StartMission()
{
    bPresetError = !RegimentMissionPresets::Validate(Draft, PresetStatus);
    RegimentAudioSettings::PlayMenuSound(World, bPresetError
        ? RegimentAudioSettings::EMenuSound::Error : RegimentAudioSettings::EMenuSound::Confirm);
    if (!bPresetError && OnStart)
        OnStart(Draft);
    return FReply::Handled();
}

TSharedRef<SWidget> SRegimentMissionHud::MakePresetControls()
{
    using namespace MissionHudSlate;
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
        [
            SNew(SBox).Visibility_Lambda([this]
            {
                return Draft.EditableCopyId.IsValid() ? EVisibility::Visible : EVisibility::Collapsed;
            })
            [
                MakeControl(LOCTEXT("SavePreset", "Сохранить"),
                    LOCTEXT("SavePresetHelp", "Записать изменения в открытую копию сценария"),
                    128.f, PresetControlHeight, FSlateColor(ControlTint),
                    FOnClicked::CreateSP(this, &SRegimentMissionHud::SaveActivePreset), true)
            ]
        ]
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
        [
            SNew(SBox).WidthOverride(255.f).HeightOverride(PresetControlHeight)
            [
                SAssignNew(PresetName, SEditableTextBox)
                .Style(&PresetNameStyle())
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 15))
                .Padding(FMargin(10, 3))
                .HintText(LOCTEXT("CopyName", "Название копии"))
            ]
        ]
        + SHorizontalBox::Slot().AutoWidth()
        [
            MakeControl(LOCTEXT("SavePresetCopy", "Сохранить копию"),
                LOCTEXT("SavePresetCopyHelp", "Сохранить открытый сценарий под новым именем"),
                200.f, PresetControlHeight, FSlateColor(ControlTint),
                FOnClicked::CreateSP(this, &SRegimentMissionHud::SavePresetCopy), true)
        ];
}

TSharedRef<SWidget> SRegimentMissionHud::MakeTerrainSelector()
{
    using battle::MarchTerrain;
    const auto Top = SNew(SHorizontalBox);
    const auto Bottom = SNew(SHorizontalBox);
    const auto Historic = SNew(SHorizontalBox);
    const auto EuropeanBattlefields = SNew(SHorizontalBox);
    const auto OtherBattlefields = SNew(SHorizontalBox);
    const auto EraBattlefields = SNew(SVerticalBox);
    const auto AddOption = [this](const TSharedRef<SHorizontalBox>& Row, MarchTerrain Option)
    {
        const FText Label = MissionHudSlate::TerrainName(Option);
        Row->AddSlot().AutoWidth().Padding(0, 0, 8, 0)
        [
            MissionHudSlate::MakeControl(Label, Label, Option == MarchTerrain::StirlingBridge ? 218.f : 188.f, 38.f,
                TAttribute<FSlateColor>::CreateLambda([this, Option] {
                    return FSlateColor(Draft.Terrain == Option ? MissionHudSlate::SelectedTint : MissionHudSlate::ControlTint);
                }),
                FOnClicked::CreateLambda([this, Option]
                {
                    ChooseTerrain(Option);
                    return FReply::Handled();
                }), true)
        ];
    };
    AddOption(Top, MarchTerrain::River);
    AddOption(Top, MarchTerrain::OpenField);
    AddOption(Top, MarchTerrain::Hill);
    AddOption(Bottom, MarchTerrain::StoneWalls);
    AddOption(Bottom, MarchTerrain::StirlingBridge);
    AddOption(Historic, MarchTerrain::Cannae);
    AddOption(Historic, MarchTerrain::Peipus);
    AddOption(EuropeanBattlefields, MarchTerrain::Agincourt);
    AddOption(EuropeanBattlefields, MarchTerrain::Pavia);
    AddOption(EuropeanBattlefields, MarchTerrain::Waterloo);
    AddOption(OtherBattlefields, MarchTerrain::Poltava);
    AddOption(OtherBattlefields, MarchTerrain::Nagashino);
    AddOption(OtherBattlefields, MarchTerrain::Sekigahara);
    struct FEraBattlefields
    {
        const TCHAR* Era;
        MarchTerrain First;
        TOptional<MarchTerrain> Second;
    };
    const FEraBattlefields EraOptions[] = {
        {TEXT("ancient"), MarchTerrain::Gaugamela, MarchTerrain::Zama},
        {TEXT("high_medieval"), MarchTerrain::Dorylaeum, MarchTerrain::Kalka},
        {TEXT("late_medieval"), MarchTerrain::Crecy, MarchTerrain::Grunwald},
        {TEXT("renaissance"), MarchTerrain::Breitenfeld, MarchTerrain::Lutzen},
        {TEXT("sengoku"), MarchTerrain::Kawanakajima, {}},
        {TEXT("great_northern_war"), MarchTerrain::Narva, MarchTerrain::Lesnaya},
        {TEXT("napoleonic"), MarchTerrain::Austerlitz, MarchTerrain::Borodino},
    };
    for (const auto& Options : EraOptions)
    {
        const auto Row = SNew(SHorizontalBox).Visibility_Lambda([this, Era = FString(Options.Era)]
        {
            return Draft.Era.Id == Era ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
        });
        AddOption(Row, Options.First);
        if (Options.Second)
            AddOption(Row, *Options.Second);
        EraBattlefields->AddSlot().AutoHeight()[Row];
    }
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 7)[Top]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 7)[Bottom]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 7)[Historic]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 7)[EuropeanBattlefields]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 7)[OtherBattlefields]
        + SVerticalBox::Slot().AutoHeight()[EraBattlefields];
}

TSharedRef<SWidget> SRegimentMissionHud::MakeFieldSizeControls()
{
    using namespace MissionHudSlate;
    const auto Controls = SNew(SHorizontalBox);
    const auto AddDimension = [this, &Controls](const FText& Label, bool bWidth, int32 Min, int32 Max)
    {
        const auto UpdateDimension = [this, bWidth, Min, Max](int32 Value)
        {
            auto Field = Draft.Field;
            (bWidth ? Field.width_m : Field.height_m) = FMath::Clamp(Value, Min, Max);
            ChooseFieldSize(Field);
        };
        Controls->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
        [MakeText(Label, 15, FSlateColor(Muted), true)];
        Controls->AddSlot().AutoWidth().Padding(0, 0, 18, 0)
        [
            MakeField<int32>(90.f, Min, Max,
                FText::Format(LOCTEXT("FieldDimensionHelp", "Допустимый размер от {0} до {1} м"), Min, Max),
                TAttribute<int32>::CreateLambda([this, bWidth] { return bWidth ? Draft.Field.width_m : Draft.Field.height_m; }),
                SSpinBox<int32>::FOnValueChanged::CreateLambda(UpdateDimension),
                SSpinBox<int32>::FOnValueCommitted::CreateLambda([this, UpdateDimension](int32 Value, ETextCommit::Type)
                {
                    UpdateDimension(Value);
                    if (OnTerrainChanged)
                        OnTerrainChanged(Draft.Terrain, Draft.Field);
                }))
        ];
    };
    AddDimension(LOCTEXT("FieldWidth", "Ширина, м"), true, MissionMinFieldWidth, MissionMaxFieldWidth);
    AddDimension(LOCTEXT("FieldHeight", "Глубина, м"), false, MissionMinFieldHeight, MissionMaxFieldHeight);
    return Controls;
}

void SRegimentMissionHud::SetSetupStep(ESetupStep Step)
{
    if (Step == ESetupStep::Scenario || !ScenarioPreview)
    {
        SetupStep = Step;
        Invalidate(EInvalidateWidgetReason::Layout);
    }
}

FText SRegimentMissionHud::NextStepLabel() const
{
    switch (SetupStep)
    {
        case ESetupStep::Scenario:
            return ScenarioPreview ? LOCTEXT("ApplyScenario", "Выбрать и продолжить")
                                   : LOCTEXT("ContinueToMap", "К карте и цели");
        case ESetupStep::Battlefield: return LOCTEXT("ContinueToArmies", "К армиям");
        case ESetupStep::Armies: return LOCTEXT("ContinueToDeployment", "К расстановке");
        case ESetupStep::Deployment: return LOCTEXT("ContinueToLaunch", "К запуску");
        case ESetupStep::Launch:
            return Draft.StartsInBattle() ? LOCTEXT("StartBattle", "Начать бой") : LOCTEXT("StartMarch", "Начать поход");
    }
    return FText::GetEmpty();
}

FText SRegimentMissionHud::SetupStatus() const
{
    if (!PresetStatus.IsEmpty())
        return PresetStatus;
    if (ScenarioPreview)
        return LOCTEXT("ScenarioPending", "При продолжении загрузятся исходные настройки выбранного сценария.");
    return FText::GetEmpty();
}

FReply SRegimentMissionHud::AdvanceSetup()
{
    if (SetupStep == ESetupStep::Launch)
        return StartMission();

    if (SetupStep == ESetupStep::Scenario && ScenarioPreview)
    {
        auto Preset = MoveTemp(ScenarioPreview.GetValue());
        SetPreset(Preset);
        ClearPresetNotice();
        if (OnTerrainChanged)
            OnTerrainChanged(Draft.Terrain, Draft.Field);
    }
    SetSetupStep(static_cast<ESetupStep>(static_cast<int32>(SetupStep) + 1));
    RegimentAudioSettings::PlayMenuSound(World, RegimentAudioSettings::EMenuSound::Confirm);
    return FReply::Handled();
}

TSharedRef<SWidget> SRegimentMissionHud::MakeSetupSteps()
{
    using namespace MissionHudSlate;
    const auto Steps = SNew(SHorizontalBox);
    const auto AddStep = [this, &Steps](ESetupStep Step, FText Name)
    {
        FText Label = FText::Format(LOCTEXT("StepNumberAndName", "{0}. {1}"), static_cast<int32>(Step) + 1, Name);
        Steps->AddSlot().FillWidth(1.f).Padding(0, 0, 6, 0)
        [
            SNew(SButton)
            .ButtonStyle(&ControlStyle())
            .ContentPadding(FMargin(12, 10))
            .HAlign(HAlign_Left)
            .ButtonColorAndOpacity_Lambda([this, Step]
            {
                return FSlateColor(SetupStep == Step ? SelectedTint : ControlTint);
            })
            .IsEnabled_Lambda([this, Step] { return !ScenarioPreview || Step == ESetupStep::Scenario; })
            .OnClicked_Lambda([this, Step]
            {
                RegimentAudioSettings::PlayMenuSound(World, Step < SetupStep
                    ? RegimentAudioSettings::EMenuSound::Back : RegimentAudioSettings::EMenuSound::Click);
                SetSetupStep(Step);
                return FReply::Handled();
            })
            [MakeText(Label, 16, FSlateColor(Ink), true)]
        ];
    };
    AddStep(ESetupStep::Scenario, LOCTEXT("ScenarioStep", "Сценарий"));
    AddStep(ESetupStep::Battlefield, LOCTEXT("BattlefieldStep", "Карта и цель"));
    AddStep(ESetupStep::Armies, LOCTEXT("ArmiesStep", "Армии"));
    AddStep(ESetupStep::Deployment, LOCTEXT("DeploymentStep", "Расстановка"));
    AddStep(ESetupStep::Launch, LOCTEXT("LaunchStep", "Запуск"));
    return Steps;
}

TSharedRef<SWidget> SRegimentMissionHud::MakeFactionTab()
{
    using namespace MissionHudSlate;
    const auto FactionPicker = [this](bool bBlue, const FText& Label, const FSlateColor& Color)
    {
        return MakeFactionPicker(Label, Color, TAttribute<FText>::CreateLambda([this, bBlue]
            {
                const FRegimentEraDefinition* Era = RegimentEraCatalog::Find(FStringView(ModuleId));
                if (Era)
                {
                    const FString& Id = bBlue ? Draft.Era.BlueFaction : Draft.Era.OrangeFaction;
                    if (const FRegimentEraFaction* Faction = RegimentEraCatalog::FindFaction(*Era, FStringView(Id)))
                        return FText::FromString(Faction->Name);
                }
                return LOCTEXT("NoFaction", "Без фракции");
            }),
            FOnGetContent::CreateSP(this, &SRegimentMissionHud::MakeFactionMenu, bBlue),
            TAttribute<bool>::CreateLambda([this] { return Draft.Era.IsSet(); }));
    };

    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0, 0, 16, 0)
        [FactionPicker(true, LOCTEXT("BlueFaction", "Фракция синих"), FSlateColor(Blue))]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
        [FactionPicker(false, LOCTEXT("OrangeFaction", "Фракция оранжевых"), FSlateColor(Orange))];
}

TSharedRef<SWidget> SRegimentMissionHud::MakeFactionMenu(bool bBlue)
{
    FMenuBuilder Menu(true, nullptr);
    if (const FRegimentEraDefinition* Era = RegimentEraCatalog::Find(FStringView(ModuleId)))
    {
        const FString& Selected = bBlue ? Draft.Era.BlueFaction : Draft.Era.OrangeFaction;
        for (const FRegimentEraFaction& Faction : Era->Factions)
        {
            Menu.AddMenuEntry(FText::FromString(Faction.Name), FText::GetEmpty(), FSlateIcon(),
                FUIAction(FExecuteAction::CreateSP(this, &SRegimentMissionHud::ChooseFaction, bBlue, Faction.Id),
                    FCanExecuteAction(), FIsActionChecked::CreateLambda([bSelected = Faction.Id == Selected] { return bSelected; })),
                NAME_None, EUserInterfaceActionType::RadioButton);
        }
    }
    return Menu.MakeWidget();
}

TSharedRef<SWidget> SRegimentMissionHud::MakeObjectiveControls()
{
    using namespace MissionHudSlate;
    const auto Choices = SNew(SHorizontalBox);
    const EMissionGoal Goals[] = {EMissionGoal::Eliminate, EMissionGoal::Crossing,
        EMissionGoal::ReserveExercise, EMissionGoal::HillHold};
    for (EMissionGoal Goal : Goals)
    {
        Choices->AddSlot().AutoWidth().Padding(0, 0, 8, 0)
        [
            MakeControl(GoalName(Goal), GoalDescription(Goal), Goal == EMissionGoal::HillHold ? 190.f : 151.f, 35.f,
                TAttribute<FSlateColor>::CreateLambda([this, Goal]
                {
                    return FSlateColor(Draft.Objective.Goal == Goal ? SelectedTint : ControlTint);
                }),
                FOnClicked::CreateLambda([this, Goal]
                {
                    Draft.Objective.Goal = Goal;
                    if (Goal == EMissionGoal::HillHold)
                        Draft.Objective.TimeLimitSeconds = FMath::Max(Draft.Objective.TimeLimitSeconds, MissionHillHoldSeconds);
                    ClearPresetNotice();
                    return FReply::Handled();
                }), true)
        ];
    }

    const auto Teams = SNew(SHorizontalBox);
    for (int32 Team = 0; Team < 2; ++Team)
    {
        const FText Label = Team == 0 ? LOCTEXT("BlueForCrossing", "Синие") : LOCTEXT("OrangeForCrossing", "Оранжевые");
        Teams->AddSlot().AutoWidth().Padding(0, 0, 6, 0)
        [
            MakeControl(Label, LOCTEXT("SelectCrossingTeam", "Выбрать сторону, которая должна пройти переправу"),
                142.f, 35.f, TAttribute<FSlateColor>::CreateLambda([this, Team]
                {
                    return FSlateColor(Draft.Objective.CrossingTeam == Team ? SelectedTint : ControlTint);
                }),
                FOnClicked::CreateLambda([this, Team]
                {
                    Draft.Objective.CrossingTeam = Team;
                    ClearPresetNotice();
                    return FReply::Handled();
                }), true)
        ];
    }

    const auto CrossingVisibility = [this]
    {
        return Draft.Objective.Goal == EMissionGoal::Crossing ? EVisibility::Visible : EVisibility::Collapsed;
    };
    const auto Label = [](const FText& Text) { return MakeText(Text, 15, FSlateColor(Muted), true); };
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [Choices]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)
        [
            SNew(STextBlock)
            .Text_Lambda([this] { return GoalDescription(Draft.Objective.Goal); })
            .WrapTextAt(610.f)
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 15))
            .ColorAndOpacity(FSlateColor(Muted))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(SHorizontalBox)
            .Visibility_Lambda([this]
            {
                return Draft.Objective.Goal == EMissionGoal::Eliminate ? EVisibility::Collapsed : EVisibility::Visible;
            })
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
            [Label(LOCTEXT("ObjectiveTime", "Время, с"))]
            + SHorizontalBox::Slot().AutoWidth()
            [
                MakeField<int32>(90.f, TAttribute<TOptional<int32>>::CreateLambda([this]() -> TOptional<int32>
                    {
                        return Draft.Objective.Goal == EMissionGoal::HillHold ? MissionHillHoldSeconds : MissionMinTimeLimit;
                    }), MissionMaxTimeLimit, LOCTEXT("ObjectiveTimeHelp", "Отведённое на цель время в секундах"),
                    TAttribute<int32>::CreateLambda([this] { return Draft.Objective.TimeLimitSeconds; }),
                    SSpinBox<int32>::FOnValueChanged::CreateLambda([this](int32 Value)
                    {
                        Draft.Objective.TimeLimitSeconds = Value;
                        ClearPresetNotice();
                    }))
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [
            SNew(SHorizontalBox)
            .Visibility_Lambda(CrossingVisibility)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
            [Label(LOCTEXT("CrossingTeam", "Сторона"))]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 20, 0)
            [Teams]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
            [Label(LOCTEXT("CrossingRequired", "Полков для цели"))]
            + SHorizontalBox::Slot().AutoWidth()
            [
                MakeField<int32>(74.f, 1, MissionRegimentsPerTeam,
                    LOCTEXT("CrossingRequiredHelp", "Сколько полков выбранной стороны должны пройти переправу"),
                    TAttribute<int32>::CreateLambda([this] { return Draft.Objective.RequiredRegiments; }),
                    SSpinBox<int32>::FOnValueChanged::CreateLambda([this](int32 Value)
                    {
                        Draft.Objective.RequiredRegiments = Value;
                        ClearPresetNotice();
                    }))
            ]
        ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SHorizontalBox)
            .Visibility_Lambda(CrossingVisibility)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
            [Label(LOCTEXT("CrossingPoint", "Переправа"))]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
            [
                SNew(SBox)
                .Visibility_Lambda([this] { return Draft.Terrain == battle::MarchTerrain::StirlingBridge
                    ? EVisibility::Collapsed : EVisibility::Visible; })
                [
                    MakeField<int32>(74.f, 1, static_cast<int32>(battle::MarchCrossings.size()),
                        LOCTEXT("CrossingPointHelp", "Номер переправы на карте"),
                        TAttribute<int32>::CreateLambda([this] { return Draft.Objective.Crossing + 1; }),
                        SSpinBox<int32>::FOnValueChanged::CreateLambda([this](int32 Value)
                        {
                            Draft.Objective.Crossing = Value - 1;
                            ClearPresetNotice();
                        }))
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                MakeText(TAttribute<FText>::CreateLambda([this]
                {
                    if (Draft.Terrain == battle::MarchTerrain::StirlingBridge)
                        return LOCTEXT("StirlingCrossingName", "Стерлингский мост");
                    const int32 Index = FMath::Clamp(Draft.Objective.Crossing, 0, static_cast<int32>(battle::MarchCrossings.size()) - 1);
                    return FText::Format(LOCTEXT("CrossingPointY", "Y = {0} м"),
                        FText::AsNumber(battle::MarchCrossings[Index].y));
                }), 15, FSlateColor(Muted))
            ]
        ];
}

void SRegimentMissionHud::RefreshBriefingSources()
{
    using namespace MissionHudSlate;
    if (!BriefingSources)
        return;
    BriefingSources->ClearChildren();
    const auto& Sources = PreviewedPreset().Sources;
    if (Sources.IsEmpty())
        return;
    BriefingSources->AddSlot().AutoHeight().Padding(0, 10, 0, 6)
    [MakeText(LOCTEXT("BriefingSources", "Источники"), 15, FSlateColor(Brass), true)];
    for (const FString& Source : Sources)
    {
        const FText UrlText = FText::FromString(Source);
        bool bCanOpen = Source.StartsWith(TEXT("https://"), ESearchCase::IgnoreCase) && Source.Len() > 8;
        for (TCHAR Character : Source)
            bCanOpen &= !FChar::IsControl(Character) && !FChar::IsWhitespace(Character);
        if (bCanOpen)
        {
            FText LinkLabel = UrlText;
            if (Source.StartsWith(TEXT("https://portal.historicenvironment.scot/"), ESearchCase::IgnoreCase))
            {
                if (Source.EndsWith(TEXT(",BTL28")))
                    LinkLabel = LOCTEXT("HistoricalBattleInventory", "Реестр поля сражения (BTL28)");
                else if (Source.EndsWith(TEXT(",SM8264")))
                    LinkLabel = LOCTEXT("HistoricalBridgeRecord", "Средневековый мост (SM8264)");
            }
            BriefingSources->AddSlot().AutoHeight().HAlign(HAlign_Left).Padding(0, 0, 0, 4)
            [
                SNew(SHyperlink)
                .Text(LinkLabel)
                .ToolTipText(UrlText)
                .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                .OnNavigate_Lambda([Source] { FPlatformProcess::LaunchURL(*Source, nullptr, nullptr); })
            ];
        }
        else
        {
            BriefingSources->AddSlot().AutoHeight()
            [MakeText(UrlText, 13, FSlateColor(Muted))];
        }
    }
}

TSharedRef<SWidget> SRegimentMissionHud::MakeScenarioStep()
{
    using namespace MissionHudSlate;
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 17, 0)
        [
            SNew(SBox).WidthOverride(215.f)[MakeScenarioEras()]
        ]
        + SHorizontalBox::Slot().FillWidth(.92f).Padding(0, 0, 16, 0)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 9)
            [
                SAssignNew(ScenarioSearch, SSearchBox)
                .Style(&AtlasSearchStyle())
                .HintText(LOCTEXT("SearchScenarios", "Найти битву"))
                .DelayChangeNotificationsWhileTyping(false)
                .OnTextChanged(this, &SRegimentMissionHud::ChangeScenarioSearch)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [MakeScenarioFilters()]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
            [
                MakeText(TAttribute<FText>::CreateLambda([this] { return ScenarioResultCount; }),
                    13, FSlateColor(Muted))
            ]
            + SVerticalBox::Slot().FillHeight(1.f)
            [
                SNew(SScrollBox)
                + SScrollBox::Slot().Padding(0, 0, 8, 0)
                [SAssignNew(ScenarioList, SVerticalBox)]
            ]
        ]
        + SHorizontalBox::Slot().FillWidth(1.08f)
        [
            SNew(SBorder).BorderImage(&AtlasBriefing).Padding(FMargin(17, 15))
            [
                SNew(SScrollBox)
                + SScrollBox::Slot().Padding(0, 0, 12, 0)
                [
                    SNew(SBox).HeightOverride(146.f).Padding(0, 0, 0, 12)
                    .Visibility_Lambda([this]
                    {
                        const auto& Preset = PreviewedPreset();
                        return Preset.Terrain == battle::MarchTerrain::Waterloo &&
                            Preset.Name.StartsWith(TEXT("Ватерлоо,"))
                            ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                    })
                    [
                        SNew(SOverlay)
                        + SOverlay::Slot()
                        [
                            SNew(SScaleBox).Stretch(EStretch::ScaleToFill)
                            .Clipping(EWidgetClipping::ClipToBoundsAlways)
                            [SNew(SImage).Image(WaterlooAtlasArt())]
                        ]
                        + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom)
                        [
                            SNew(SBorder).BorderImage(&Panel).Padding(FMargin(7, 3))
                            [MakeText(LOCTEXT("WaterlooSketch", "Эскиз Ватерлоо"), 11, FSlateColor(Ink))]
                        ]
                    ]
                ]
                + SScrollBox::Slot().Padding(0, 0, 12, 12)
                [
                    SNew(STextBlock)
                    .Text_Lambda([this] { return FText::FromString(PreviewedPreset().Name); })
                    .ColorAndOpacity(Ink)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 28))
                    .AutoWrapText(true)
                ]
                + SScrollBox::Slot().Padding(0, 0, 0, 6)
                [Readout(TAttribute<FText>::CreateLambda([this]
                {
                    const auto& Preset = PreviewedPreset();
                    return FText::Format(LOCTEXT("ScenarioTerrain", "Шаблон карты · {0} · {1} × {2} м"),
                        TerrainName(Preset.Terrain), FText::AsNumber(Preset.Field.width_m),
                        FText::AsNumber(Preset.Field.height_m));
                }))]
                + SScrollBox::Slot().Padding(0, 0, 0, 6)
                [Readout(TAttribute<FText>::CreateLambda([this]
                {
                    return FText::Format(LOCTEXT("ScenarioGoal", "Цель · {0}"), GoalName(PreviewedPreset().Objective.Goal));
                }))]
                + SScrollBox::Slot().Padding(0, 0, 0, 16)
                [Readout(TAttribute<FText>::CreateLambda([this]
                {
                    const auto& Preset = PreviewedPreset();
                    return FText::Format(LOCTEXT("ScenarioStrength", "Фигуры · {0}"),
                        FText::AsNumber(Preset.FigureTarget > 0 ? Preset.FigureTarget :
                            MissionFigureCount(Preset.Roster, Preset.Era.IsSet())));
                }))]
                + SScrollBox::Slot()
                [Readout(TAttribute<FText>::CreateLambda([this]
                {
                    const auto& Preset = PreviewedPreset();
                    return Preset.Briefing.IsEmpty() ? GoalDescription(Preset.Objective.Goal) : FText::FromString(Preset.Briefing);
                }))]
                + SScrollBox::Slot()
                [SAssignNew(BriefingSources, SVerticalBox)]
            ]
        ];
}

TSharedRef<SWidget> SRegimentMissionHud::MakeScenarioEras()
{
    using namespace MissionHudSlate;
    const auto Rail = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 9)
        [MakeText(LOCTEXT("ScenarioEras", "Эпохи"), 16, FSlateColor(AtlasAccent), true)];
    const auto AddEra = [this, &Rail](FString Id, FText Label, bool bAvailable)
    {
        Rail->AddSlot().AutoHeight().Padding(0, 0, 0, 5)
        [
            SNew(SButton)
            .ButtonStyle(&AtlasRowStyle())
            .ButtonColorAndOpacity_Lambda([this, Id]
            {
                return FSlateColor(Id == ModuleId ? AtlasSelected : AtlasSurface);
            })
            .ContentPadding(FMargin(10, 6))
            .HAlign(HAlign_Fill)
            .IsEnabled(bAvailable)
            .OnClicked(FOnClicked::CreateSP(this, &SRegimentMissionHud::SwitchScenarioEra, Id))
            [
                SNew(STextBlock).Text(Label).AutoWrapText(true).ColorAndOpacity(Ink)
                .Font(FCoreStyle::GetDefaultFontStyle("Regular", 15))
            ]
        ];
    };
    for (const FRegimentGameModule& Module : RegimentGameModules::List())
    {
        const FText Label = Module.Id == TEXT("renaissance") ? LOCTEXT("AtlasRenaissance", "Возрождение")
            : Module.Id == TEXT("sengoku") ? LOCTEXT("AtlasSengoku", "Сэнгоку") : Module.Name;
        AddEra(Module.Id, Label, Module.bAvailable);
    }
    AddEra(FString(), RegimentGameModules::Name(FStringView()), true);
    return SNew(SScrollBox) + SScrollBox::Slot().Padding(0, 0, 8, 0)[Rail];
}

TSharedRef<SWidget> SRegimentMissionHud::MakeScenarioFilters()
{
    using namespace MissionHudSlate;
    const auto Filters = SNew(SHorizontalBox);
    const auto AddFilter = [this, &Filters](EScenarioFilter Filter, FText Label)
    {
        Filters->AddSlot().AutoWidth().Padding(0, 0, 5, 0)
        [
            SNew(SButton)
            .ButtonStyle(&AtlasRowStyle())
            .ButtonColorAndOpacity_Lambda([this, Filter]
            {
                return FSlateColor(Filter == ScenarioFilter ? AtlasSelected : AtlasSurface);
            })
            .ContentPadding(FMargin(9, 5))
            .OnClicked_Lambda([this, Filter]
            {
                ScenarioFilter = Filter;
                RebuildScenarioList();
                RegimentAudioSettings::PlayMenuSound(World, RegimentAudioSettings::EMenuSound::Click);
                return FReply::Handled();
            })
            [MakeText(Label, 14, FSlateColor(Ink), true)]
        ];
    };
    AddFilter(EScenarioFilter::All, LOCTEXT("AllScenarios", "Все"));
    AddFilter(EScenarioFilter::Historical, LOCTEXT("HistoricalScenarios", "Исторические"));
    AddFilter(EScenarioFilter::Saved, LOCTEXT("OwnScenarios", "Мои"));
    return Filters;
}

TSharedRef<SWidget> SRegimentMissionHud::MakeBattlefieldStep()
{
    using namespace MissionHudSlate;
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 24, 0)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
            [MakeText(LOCTEXT("TerrainLabel", "Карта"), 22, FSlateColor(Ink), true)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 15)
            [MakeTerrainSelector()]
            + SVerticalBox::Slot().AutoHeight()
            [MakeFieldSizeControls()]
        ]
        + SHorizontalBox::Slot().FillWidth(1.f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
            [MakeText(LOCTEXT("GoalLabel", "Цель миссии"), 22, FSlateColor(Ink), true)]
            + SVerticalBox::Slot().AutoHeight()
            [MakeObjectiveControls()]
        ];
}

TSharedRef<SWidget> SRegimentMissionHud::MakeLaunchStep()
{
    using namespace MissionHudSlate;
    const auto StartOption = [this](bool bDirect, FText Label)
    {
        return SNew(SBox)
            .IsEnabled_Lambda([this, bDirect] { return bDirect || Draft.Objective.Goal == EMissionGoal::Eliminate; })
            [
                MakeControl(Label, Label, bDirect ? 206.f : 245.f, 42.f,
                    TAttribute<FSlateColor>::CreateLambda([this, bDirect]
                    {
                        return FSlateColor(Draft.StartsInBattle() == bDirect ? SelectedTint : ControlTint);
                    }),
                    FOnClicked::CreateLambda([this, bDirect]
                    {
                        Draft.bDirectDeployment = bDirect;
                        ClearPresetNotice();
                        return FReply::Handled();
                    }), true)
            ];
    };

    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 28, 0)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
            [MakeText(LOCTEXT("OpeningMode", "Способ начала"), 22, FSlateColor(Ink), true)]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 9)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
                [StartOption(false, LOCTEXT("StartWithMarch", "Поход и обнаружение"))]
                + SHorizontalBox::Slot().AutoWidth()
                [StartOption(true, LOCTEXT("StartWithDeployment", "Бой на позициях"))]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 22)
            [
                SNew(SBox).WidthOverride(602.f)
                [Readout(TAttribute<FText>::CreateLambda([this]
                {
                    if (Draft.Objective.Goal != EMissionGoal::Eliminate)
                        return LOCTEXT("RequiresDeployment", "Для этой цели бой начинается с заданных позиций.");
                    return Draft.bDirectDeployment
                        ? LOCTEXT("DeploymentHelp", "Полки начинают бой на позициях из шага «Расстановка».")
                        : LOCTEXT("MarchOpeningHelp", "Полки идут походным строем. После обнаружения противника можно задать маршруты.");
                }))]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
            [MakeTacticalOptions()]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 9)
            [MakeText(LOCTEXT("SaveScenarioHeading", "Сохранить настройки"), 18, FSlateColor(Brass), true)]
            + SVerticalBox::Slot().AutoHeight()
            [MakePresetControls()]
        ]
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Top)
        [
            SNew(SBorder).BorderImage(&RosterRow).Padding(FMargin(21, 17))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
                [MakeText(LOCTEXT("BattleSummary", "Параметры боя"), 22, FSlateColor(Ink), true)]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [Readout(TAttribute<FText>::CreateLambda([this]
                {
                    return FText::Format(LOCTEXT("SummaryTerrain", "Шаблон карты · {0} · {1} × {2} м"),
                        TerrainName(Draft.Terrain), Draft.Field.width_m, Draft.Field.height_m);
                }))]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [Readout(TAttribute<FText>::CreateLambda([this]
                {
                    return FText::Format(LOCTEXT("ScenarioGoal", "Цель · {0}"), GoalName(Draft.Objective.Goal));
                }))]
                + SVerticalBox::Slot().AutoHeight()
                [Readout(TAttribute<FText>::CreateLambda([this]
                {
                    int32 BlueCount = 0, OrangeCount = 0;
                    for (int32 Index = 0; Index < Draft.Roster.Num(); ++Index)
                        if (Draft.Roster[Index].bEnabled)
                            ++(Index < MissionRegimentsPerTeam ? BlueCount : OrangeCount);
                    return FText::Format(LOCTEXT("SummaryRegiments", "Полки · синие {0} · оранжевые {1}"), BlueCount, OrangeCount);
                }))]
            ]
        ];
}

TSharedRef<SWidget> SRegimentMissionHud::MakeTacticalOptions()
{
    using namespace MissionHudSlate;
    const auto Checkbox = [this](FText Caption, FText Tooltip, bool FMissionPreset::*Option)
    {
        return SNew(SCheckBox)
            .IsChecked_Lambda([this, Option] { return Draft.*Option ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .IsEnabled_Lambda([this, Option]
            {
                if (Option == &FMissionPreset::bRiskyVolley)
                    return Draft.bRegimentTactics;
                return true;
            })
            .ToolTipText(Tooltip)
            .OnCheckStateChanged_Lambda([this, Option](ECheckBoxState State)
            {
                Draft.*Option = State == ECheckBoxState::Checked;
                if (!Draft.bRegimentTactics)
                    Draft.bRiskyVolley = false;
                ClearPresetNotice();
            })
            [
                MakeText(Caption, 15, FSlateColor(Ink))
            ];
    };
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
        [MakeText(LOCTEXT("TacticsHeading", "Правила боя"), 18, FSlateColor(Brass), true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
        [Checkbox(LOCTEXT("RegimentTactics", "Полковая тактика"), LOCTEXT("RegimentTacticsHint",
            "Боевой дух, расстройство строя, направления ударов и походное построение"),
            &FMissionPreset::bRegimentTactics)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 3)
        [Checkbox(LOCTEXT("RiskyVolley", "Стрельба по полкам в рукопашной"), LOCTEXT("RiskyVolleyHint",
            "Стрелы могут попасть и по своим. Действует для обеих сторон."),
            &FMissionPreset::bRiskyVolley)]
        + SVerticalBox::Slot().AutoHeight()
        [Checkbox(LOCTEXT("AdaptiveBattle", "Редкий пересмотр приказов"), LOCTEXT("AdaptiveBattleHint",
            "Командиры реже пересматривают планы. Появление угрозы, доставка приказа и контакт обрабатываются по событиям."),
            &FMissionPreset::bAdaptiveBattle)];
}

void SRegimentMissionHud::SetMarchPosture(bool bColumn, int32 DelaySeconds, int32 CrossingPosition)
{
    bTravelColumn = bColumn;
    MarchDeploymentSeconds = DelaySeconds;
    MarchCrossingPosition = CrossingPosition;
}

void SRegimentMissionHud::SetStatus(EMissionHudStage InStage, uint64 Selected)
{
    if (Stage == InStage && SelectedRegiment == Selected)
    {
        return;
    }
    MarchNotice = FText::GetEmpty();

    Stage = InStage;
    SelectedRegiment = Selected;
    Invalidate(EInvalidateWidgetReason::Layout);
}

TSharedRef<SWidget> SRegimentMissionHud::MakeRosterTable(int32 FirstIndex)
{
    using namespace MissionHudSlate;
    const bool bPlayer = FirstIndex == 0;
    const TSharedRef<SVerticalBox> RosterTable = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(12, 0, 12, 10)
        [
            MakeText(TAttribute<FText>::CreateLambda([this, FirstIndex, bPlayer]
            {
                int32 Enabled = 0;
                for (int32 Index = FirstIndex; Index < FirstIndex + MissionRegimentsPerTeam; ++Index)
                    Enabled += Draft.Roster[Index].bEnabled ? 1 : 0;
                return FText::Format(bPlayer ? LOCTEXT("BlueRegiments", "Синие полки · {0}/{1}")
                        : LOCTEXT("OrangeRegiments", "Оранжевые полки · {0}/{1}"), Enabled, MissionRegimentsPerTeam);
            }), 19, FSlateColor(bPlayer ? Blue : Orange), true)
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(12, 0, 12, 7)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, ColumnGap, 0)
            [
                MakeColumnHeading(LOCTEXT("EnabledColumn", "Вкл."), EnabledWidth, HAlign_Center)
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, MissionHudSlate::ColumnGap, 0)
            [
                MissionHudSlate::MakeColumnHeading(LOCTEXT("RegimentColumn", "Полк"), MissionHudSlate::NameWidth, HAlign_Left)
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, MissionHudSlate::ColumnGap, 0)
            [
                MakeColumnHeading(TAttribute<FText>::CreateLambda([this, FirstIndex]
                {
                    const auto* Faction = RosterFaction(FirstIndex);
                    if (RegimentUsesBayonetProfile(Faction, ERegimentEraRole::Swords))
                        return HasPlatformChoices(Faction) ? LOCTEXT("InfantryOrPlatformColumn", "Пех/тех")
                            : LOCTEXT("InfantryColumn", "Пехота");
                    return HasPlatformChoices(Faction) ? LOCTEXT("SwordsOrPlatformColumn", "Меч/тех")
                        : LOCTEXT("SwordsColumn", "Мечи");
                }), CountWidth, HAlign_Center)
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, MissionHudSlate::ColumnGap, 0)
            [
                MissionHudSlate::MakeColumnHeading(LOCTEXT("ArchersColumn", "Стрел."), MissionHudSlate::CountWidth, HAlign_Center)
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, MissionHudSlate::ColumnGap, 0)
            [
                MakeColumnHeading(TAttribute<FText>::CreateLambda([this, FirstIndex]
                {
                    return RegimentUsesBayonetProfile(RosterFaction(FirstIndex), ERegimentEraRole::Spearmen)
                        ? LOCTEXT("BayonetColumn", "Штыки") : LOCTEXT("SpearmenColumn", "Копья");
                }), CountWidth, HAlign_Center)
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, MissionHudSlate::ColumnGap, 0)
            [
                MissionHudSlate::MakeColumnHeading(LOCTEXT("MountedColumn", "Конн."), MissionHudSlate::CountWidth, HAlign_Center)
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                MissionHudSlate::MakeColumnHeading(LOCTEXT("TotalColumn", "Итог"), MissionHudSlate::TotalWidth, HAlign_Center)
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
            [
                SNew(SBox).Visibility_Lambda([this] { return ModuleId.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
                [MakeColumnHeading(LOCTEXT("TroopProfileColumn", "Тип войск"), ProfileWidth, HAlign_Left)]
            ]
        ];

    for (int32 Index = FirstIndex; Index < FirstIndex + MissionRegimentsPerTeam; ++Index)
    {
        RosterTable->AddSlot().AutoHeight().Padding(0, 0, 0, 4.f)
        [
            MakeRosterRow(Index)
        ];
    }
    return RosterTable;
}

TSharedRef<SWidget> SRegimentMissionHud::MakeModulePanel()
{
    using namespace MissionHudSlate;
    const auto Available = SNew(SVerticalBox);
    const auto Development = SNew(SVerticalBox);
    TSharedPtr<SHorizontalBox> AvailableRow;
    TSharedPtr<SHorizontalBox> DevelopmentRow;
    int32 AvailableCount = 0;
    int32 DevelopmentCount = 0;
    for (const FRegimentGameModule& Module : RegimentGameModules::List())
    {
        if (!Module.bAvailable)
        {
            if (DevelopmentCount % 2 == 0)
            {
                DevelopmentRow = SNew(SHorizontalBox);
                Development->AddSlot().AutoHeight().Padding(0, 0, 0, 8)[DevelopmentRow.ToSharedRef()];
            }
            DevelopmentRow->AddSlot().FillWidth(1.f).Padding(0, 0, 18, 0)
            [MakeText(Module.Name, 18, FSlateColor(Muted))];
            ++DevelopmentCount;
            continue;
        }

        if (AvailableCount % 2 == 0)
        {
            AvailableRow = SNew(SHorizontalBox);
            Available->AddSlot().AutoHeight().Padding(0, 0, 0, 10)[AvailableRow.ToSharedRef()];
        }
        const FText Factions = FText::Format(LOCTEXT("ModuleFactionCount", "Фракции · {0}"), Module.FactionCount);
        const auto Button = SNew(SButton)
            .ButtonStyle(&ModuleButtonStyle())
            .ContentPadding(FMargin(15.f, 11.f))
            .HAlign(HAlign_Fill)
            .IsFocusable(true)
            .OnClicked(FOnClicked::CreateSP(this, &SRegimentMissionHud::SelectModule, Module.Id))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
                [
                    SNew(STextBlock)
                    .Visibility(EVisibility::HitTestInvisible)
                    .Text(Module.Name)
                    .ColorAndOpacity(FSlateColor(Ink))
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 18))
                    .AutoWrapText(true)
                ]
                + SVerticalBox::Slot().AutoHeight()
                [MakeText(Factions, 13, FSlateColor(Muted))]
            ];
        AvailableRow->AddSlot().FillWidth(1.f).Padding(0, 0, 10, 0)[Button];
        if (!FirstModuleButton)
            FirstModuleButton = Button;
        ++AvailableCount;
    }
    Available->AddSlot().AutoHeight().Padding(0, 0, 10, 10)
    [
        SNew(SButton)
        .ButtonStyle(&ModuleButtonStyle())
        .ContentPadding(FMargin(15.f, 11.f))
        .OnClicked_Lambda([this] { ShowCampaignMenu(); return FReply::Handled(); })
        [MakeText(LOCTEXT("CampaignEntrance", "Исторические кампании"), 18, FSlateColor(Ink), true)]
    ];
    if (AvailableCount == 0)
        Available->AddSlot().AutoHeight()[MakeText(LOCTEXT("ModulesUnavailable", "Игровые модули пока недоступны."),
            17, FSlateColor(Muted))];

    TSharedPtr<SButton> Practice;
    const auto MenuActions = SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 17, 0)
        [
            SAssignNew(Practice, SButton)
            .ButtonStyle(&ControlStyle())
            .ButtonColorAndOpacity(FSlateColor(ControlTint))
            .ContentPadding(FMargin(20.f, 10.f))
            .IsFocusable(true)
            .OnClicked(FOnClicked::CreateSP(this, &SRegimentMissionHud::SelectModule, FString()))
            [MakeText(RegimentGameModules::Name(FStringView()), 18, FSlateColor(Ink), true)]
        ]
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
        [
            SNew(STextBlock).Text(LOCTEXT("PracticeSummary", "Своя битва и Стерлингский мост"))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 16)).ColorAndOpacity(Muted).AutoWrapText(true)
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10, 0, 0, 0)
        [
            SNew(SBox).HeightOverride(40.f)
            [RegimentAudioSettings::MakeSettingsButton(World, &ControlStyle(), ControlTint)]
        ]
        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0, 0, 0)
        [
            MakeControl(LOCTEXT("QuitGame", "Выйти"), LOCTEXT("QuitGameHelp", "Закрыть игру"),
                112.f, 40.f, FSlateColor(ControlTint),
                FOnClicked::CreateLambda([this]
                {
                    if (OnQuit)
                        OnQuit();
                    return FReply::Handled();
                }), true)
        ];
    if (!FirstModuleButton)
        FirstModuleButton = Practice;

    const auto Rule = []()
    {
        return SNew(SBox).HeightOverride(1.f)[SNew(SBorder).BorderImage(&ModuleRule).Padding(FMargin(0))];
    };
    return SNew(SBox).WidthOverride(630.f)
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 7)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("GameTitle", "Эпоха битв"))
            .ColorAndOpacity(Ink)
            .Font(FCoreStyle::GetDefaultFontStyle("Bold", 56))
            .ShadowOffset(FVector2D(0, 2))
            .ShadowColorAndOpacity(FLinearColor(0, 0, 0, .48f))
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 26)
        [MakeText(LOCTEXT("ModuleMenuTitle", "Выберите эпоху"), 19, FSlateColor(Brass), true)]
        + SVerticalBox::Slot().AutoHeight()
        [Available]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SVerticalBox)
            .Visibility(DevelopmentCount == 0 ? EVisibility::Collapsed : EVisibility::Visible)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 14)
            [Rule()]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
            [MakeText(LOCTEXT("ModulesInDevelopment", "В разработке"), 16, FSlateColor(Brass), true)]
            + SVerticalBox::Slot().AutoHeight()
            [Development]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 12)
        [Rule()]
        + SVerticalBox::Slot().AutoHeight()
        [MenuActions]
    ];
}

FText SRegimentMissionHud::CampaignStageStatus(int32 Campaign, int32 Mission) const
{
    if (OnCampaignComplete && OnCampaignComplete(Campaign, Mission))
        return LOCTEXT("CampaignReplay", "Победа · повторить");
    if (OnCampaignUnlocked && OnCampaignUnlocked(Campaign, Mission))
        return LOCTEXT("CampaignAvailable", "Доступно");
    return LOCTEXT("CampaignLocked", "Закрыто");
}

FText SRegimentMissionHud::GetCampaignNotice() const
{
    if (!CampaignNotice.IsEmpty())
        return CampaignNotice;
    if (OnCampaignSavePending && OnCampaignSavePending())
        return LOCTEXT("CampaignSavePending", "Не удалось сохранить прогресс на диск. Игра повторит попытку сохранения.");
    return FText::GetEmpty();
}

TSharedRef<SWidget> SRegimentMissionHud::MakeCampaignPanel()
{
    using namespace MissionHudSlate;
    const auto& Routes = RegimentHistoricalCampaign::Catalog();
    const auto Columns = SNew(SHorizontalBox);
    for (int32 Campaign = 0; Campaign < Routes.Num(); ++Campaign)
    {
        const auto& Route = Routes[Campaign];
        const auto Column = SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
            [
                SNew(SBox).MinDesiredHeight(52.f).VAlign(VAlign_Top)
                [
                    SNew(STextBlock).Text(Route.Title).WrapTextAt(295.f)
                    .Font(FCoreStyle::GetDefaultFontStyle("Bold", 21)).ColorAndOpacity(FSlateColor(Ink))
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)
            [
                SNew(SBox).MinDesiredHeight(52.f).VAlign(VAlign_Top)
                [
                    SNew(STextBlock).Text(Route.Description).WrapTextAt(295.f)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 15)).ColorAndOpacity(FSlateColor(Muted))
                ]
            ];
        for (int32 Mission = 0; Mission < Route.Stages.Num(); ++Mission)
        {
            Column->AddSlot().AutoHeight().Padding(0, 0, 0, 8)
            [
                SNew(SButton)
                .ButtonStyle(&ModuleButtonStyle())
                .ContentPadding(FMargin(11.f, 7.f))
                .HAlign(HAlign_Fill)
                .IsEnabled_Lambda([this, Campaign, Mission]
                { return OnCampaignUnlocked && OnCampaignUnlocked(Campaign, Mission); })
                .OnClicked_Lambda([this, Campaign, Mission]
                {
                    if (OnStartCampaign)
                        OnStartCampaign(Campaign, Mission);
                    return FReply::Handled();
                })
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [MakeText(Route.Stages[Mission].Label, 17, FSlateColor(Ink), true)]
                    + SVerticalBox::Slot().AutoHeight()
                    [MakeText(TAttribute<FText>::CreateLambda([this, Campaign, Mission]
                    { return CampaignStageStatus(Campaign, Mission); }), 13, FSlateColor(Muted))]
                ]
            ];
        }
        Columns->AddSlot().FillWidth(1.f).Padding(0, 0, 16, 0)[Column];
    }
    return SNew(SBox).WidthOverride(1010.f)
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
        [MakeText(LOCTEXT("CampaignTitle", "Исторические кампании"), 35, FSlateColor(Brass), true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 20)
        [MakeText(LOCTEXT("CampaignRule", "Победа открывает следующую битву. Пройденные битвы можно повторить."),
            17, FSlateColor(Muted))]
        + SVerticalBox::Slot().AutoHeight()[Columns]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 8)
        [MakeText(TAttribute<FText>::CreateLambda([this] { return GetCampaignNotice(); }), 15, FSlateColor(Brass))]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
        [
            MakeControl(LOCTEXT("CampaignBack", "В атлас"), FText::GetEmpty(), 120.f, 40.f, FSlateColor(ControlTint),
                FOnClicked::CreateLambda([this] { ShowModuleMenu(); return FReply::Handled(); }), true)
        ]
    ];
}

TSharedRef<SWidget> SRegimentMissionHud::MakeSetupPanel()
{
    using namespace MissionHudSlate;
    const auto TwoTeams = [](const TSharedRef<SWidget>& BlueTable, const TSharedRef<SWidget>& OrangeTable)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Top).Padding(0, 0, 22, 0)
            [BlueTable]
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Top)
            [OrangeTable];
    };
    const auto Scroll = [](const TSharedRef<SWidget>& Content)
    {
        return SNew(SScrollBox) + SScrollBox::Slot().Padding(0, 0, 12, 0)[Content];
    };
    static const FButtonStyle PrimaryStyle = FButtonStyle(ControlStyle())
        .SetNormal(FSlateRoundedBoxBrush(Brass, 8.f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(FColor::FromHex(TEXT("FFDBA8"))), 8.f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(FColor::FromHex(TEXT("D4A96F"))), 8.f));
    return SNew(SBorder)
        .BorderImage(&Panel)
        .Padding(FMargin(24, 20))
        [
            SNew(SBox).WidthOverride(EditorWidth)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [
                        MakeText(TAttribute<FText>::CreateLambda([this]
                        {
                            return RegimentGameModules::Name(FStringView(ModuleId));
                        }), 28, FSlateColor(Brass), true)
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(24, 0, 20, 0)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this] { return FText::FromString(Draft.Name); })
                        .Justification(ETextJustify::Right)
                        .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 17))
                        .ColorAndOpacity(Muted)
                    ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [
                        MakeControl(LOCTEXT("ReturnToModules", "К эпохам"), LOCTEXT("ReturnToModulesHelp", "Вернуться к выбору эпохи"),
                            126.f, 38.f, FSlateColor(ControlTint),
                            FOnClicked::CreateSP(this, &SRegimentMissionHud::ReturnToModules), true)
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 20)
                [MakeSetupSteps()]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SBox).HeightOverride(490.f)
                    [
                        SNew(SWidgetSwitcher)
                        .WidgetIndex_Lambda([this] { return static_cast<int32>(SetupStep); })
                        + SWidgetSwitcher::Slot()
                        [MakeScenarioStep()]
                        + SWidgetSwitcher::Slot()
                        [Scroll(MakeBattlefieldStep())]
                        + SWidgetSwitcher::Slot()
                        [
                            Scroll(SNew(SVerticalBox)
                                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
                                [
                                    SNew(SBox)
                                    .Visibility_Lambda([this] { return ModuleId.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
                                    [MakeFactionTab()]
                                ]
                                + SVerticalBox::Slot().AutoHeight()
                                [TwoTeams(MakeRosterTable(0), MakeRosterTable(MissionRegimentsPerTeam))]
                                + SVerticalBox::Slot().AutoHeight().Padding(0, 9, 0, 0)
                                [
                                    MakeText(TAttribute<FText>::CreateLambda([this]
                                    {
                                        return Draft.FigureTarget > 0
                                            ? FText::Format(LOCTEXT("ScaledRoster", "Таблица задаёт пропорции. В бою {0} фигур с командным составом."),
                                                FText::AsNumber(Draft.FigureTarget))
                                            : LOCTEXT("FixedStaff", "Нажмите на тип полка, чтобы изменить его. До 100 фигур с командованием.");
                                    }), 14, FSlateColor(Muted))
                                ])
                        ]
                        + SWidgetSwitcher::Slot()
                        [
                            Scroll(SNew(SVerticalBox)
                                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
                                [MakeDeploymentPreview()]
                                + SVerticalBox::Slot().AutoHeight()
                                [TwoTeams(MakeDeploymentTable(0), MakeDeploymentTable(MissionRegimentsPerTeam))]
                                + SVerticalBox::Slot().AutoHeight().Padding(0, 9, 0, 0)
                                [
                                    MakeText(LOCTEXT("DeploymentHint", "X идёт между сторонами, Y — вдоль фронта. «Авто» возвращает стандартную точку полка."),
                                        14, FSlateColor(Muted))
                                ])
                        ]
                        + SWidgetSwitcher::Slot()
                        [Scroll(MakeLaunchStep())]
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 0)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)
                    [
                        SNew(SBox).Visibility_Lambda([this]
                        {
                            return SetupStep == ESetupStep::Scenario ? EVisibility::Collapsed : EVisibility::Visible;
                        })
                        [
                            MakeControl(LOCTEXT("PriorStep", "Назад"), LOCTEXT("PriorStepHelp", "К предыдущему шагу"),
                                112.f, 46.f, FSlateColor(ControlTint),
                                FOnClicked::CreateLambda([this]
                                {
                                    if (SetupStep != ESetupStep::Scenario)
                                        SetSetupStep(static_cast<ESetupStep>(static_cast<int32>(SetupStep) - 1));
                                    return FReply::Handled();
                                }), true)
                        ]
                    ]
                    + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0, 0, 18, 0)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this] { return SetupStatus(); })
                        .Font(FCoreStyle::GetDefaultFontStyle("Regular", 15))
                        .ColorAndOpacity_Lambda([this] { return FSlateColor(bPresetError ? Orange : Muted); })
                        .AutoWrapText(true)
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SBox).WidthOverride(245.f).HeightOverride(46.f)
                        [
                            SNew(SButton)
                            .ButtonStyle(&PrimaryStyle)
                            .HAlign(HAlign_Center)
                            .VAlign(VAlign_Center)
                            .OnClicked(FOnClicked::CreateSP(this, &SRegimentMissionHud::AdvanceSetup))
                            [
                                MakeText(TAttribute<FText>::CreateLambda([this] { return NextStepLabel(); }),
                                    17, FSlateColor(FLinearColor(FColor::FromHex(TEXT("231A10")))), true)
                            ]
                        ]
                    ]
                ]
            ]
        ];
}

const FRegimentEraFaction* SRegimentMissionHud::RosterFaction(int32 Index) const
{
    const auto* Era = RegimentEraCatalog::Find(FStringView(ModuleId));
    if (!Era)
        return nullptr;
    return RegimentEraCatalog::FindFaction(*Era,
        FStringView(Index < MissionRegimentsPerTeam ? Draft.Era.BlueFaction : Draft.Era.OrangeFaction));
}

TSharedRef<SWidget> SRegimentMissionHud::MakeRosterProfileMenu(int32 Index)
{
    FMenuBuilder Menu(true, nullptr);
    const auto* Faction = RosterFaction(Index);
    if (!Faction)
        return Menu.MakeWidget();
    auto& Row = Draft.Roster[Index];
    const auto AddOption = [this, &Menu](auto& Field, auto Choice, const FRegimentEraTroop* Profile)
    {
        if (!Profile)
            return;
        const bool bDefault = uint8(Choice) == 0;
        const FText Name = bDefault ? FText::Format(LOCTEXT("DefaultTroopProfile", "По фракции · {0}"),
            FText::FromString(Profile->Name)) : FText::FromString(Profile->Name);
        Menu.AddMenuEntry(Name, FText::FromString(Profile->Description), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this, &Field, Choice]
            {
                Field = Choice;
                ClearPresetNotice();
            }), FCanExecuteAction(), FIsActionChecked::CreateLambda([&Field, Choice] { return Field == Choice; })),
            NAME_None, EUserInterfaceActionType::RadioButton);
    };
    if (Row.Platform != battle::BattlePlatform::None)
    {
        for (bool bMounted : {false, true})
        {
            Menu.AddMenuEntry(bMounted ? LOCTEXT("OrdinaryCavalry", "Конный полк") : LOCTEXT("OrdinaryInfantry", "Пеший полк"),
                FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, &Row, bMounted]
                {
                    MissionHudSlate::ResetPlatformRow(Row, bMounted);
                    ClearPresetNotice();
                })));
        }
    }
    else if (Row.bCavalry)
        for (uint8 Type = 0; Type != uint8(ERegimentMountClass::Count); ++Type)
        {
            const auto Choice = ERegimentMountClass(Type);
            AddOption(Row.MountedClass, Choice, RegimentEraCatalog::MountedTroop(*Faction, Choice));
        }
    else
    {
        if (!Faction->InfantryProfiles.IsEmpty())
        {
            Menu.BeginSection(TEXT("InfantryProfiles"), LOCTEXT("InfantryProfiles", "Тип пехотного полка"));
            const auto AddInfantry = [this, &Menu, &Row](const FString& Id, const FText& Name, const FText& Help)
            {
                Menu.AddMenuEntry(Name, Help, FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, &Row, Id]
                {
                    Row.InfantryProfile = Id;
                    Row.RangedClass = ERegimentRangedClass::FactionDefault;
                    ClearPresetNotice();
                }), FCanExecuteAction(), FIsActionChecked::CreateLambda([&Row, Id]
                    { return Row.InfantryProfile == Id; })), NAME_None, EUserInterfaceActionType::RadioButton);
            };
            AddInfantry(FString(), LOCTEXT("MixedInfantry", "Смешанный состав"),
                LOCTEXT("MixedInfantryHelp", "Выбирайте число мечников, копейщиков и стрелков отдельно."));
            TArray<FString> Profiles;
            Faction->InfantryProfiles.GetKeys(Profiles);
            Profiles.Sort();
            for (const auto& Id : Profiles)
            {
                const auto& Profile = Faction->InfantryProfiles.FindChecked(Id);
                AddInfantry(Id, FText::FromString(Profile.Troop.Name),
                    FText::FromString(Profile.Troop.Description));
            }
            Menu.EndSection();
        }
        Menu.BeginSection(TEXT("MixedRangedProfiles"), LOCTEXT("MixedRangedProfiles", "Стрелки смешанного полка"));
        for (uint8 Type = 0; Type != uint8(ERegimentRangedClass::Count); ++Type)
        {
            const auto Choice = ERegimentRangedClass(Type);
            const auto* Profile = RegimentEraCatalog::RangedTroop(*Faction, Choice);
            if (!Profile)
                continue;
            const FText Name = Type == 0 ? FText::Format(LOCTEXT("DefaultRangedProfile", "По фракции · {0}"),
                FText::FromString(Profile->Name)) : FText::FromString(Profile->Name);
            Menu.AddMenuEntry(Name, FText::FromString(Profile->Description), FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([this, &Row, Choice]
                {
                    Row.InfantryProfile.Reset();
                    Row.RangedClass = Choice;
                    ClearPresetNotice();
                }), FCanExecuteAction(), FIsActionChecked::CreateLambda([&Row, Choice]
                    { return Row.InfantryProfile.IsEmpty() && Row.RangedClass == Choice; })),
                NAME_None, EUserInterfaceActionType::RadioButton);
        }
        Menu.EndSection();
    }

    if (MissionHudSlate::HasPlatformChoices(Faction))
    {
        Menu.BeginSection(TEXT("Platforms"), LOCTEXT("PlatformOptions", "Боевая техника"));
        for (int32 Type = 1; Type < MissionPlatformTypeCount; ++Type)
        {
            const auto Platform = static_cast<battle::BattlePlatform>(Type);
            if (const auto* Profile = RegimentEraCatalog::PlatformTroop(*Faction, Platform))
            {
                Menu.AddMenuEntry(FText::FromString(Profile->Name), FText::FromString(Profile->Description),
                    FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, &Row, Platform]
                    {
                        Row.Platform = Platform;
                        Row = Row.Clamped();
                        ClearPresetNotice();
                    }), FCanExecuteAction(), FIsActionChecked::CreateLambda([&Row, Platform] { return Row.Platform == Platform; })),
                    NAME_None, EUserInterfaceActionType::RadioButton);
            }
        }
        Menu.EndSection();
    }
    return Menu.MakeWidget();
}

TSharedRef<SWidget> SRegimentMissionHud::MakeRosterRow(int32 RegimentIndex)
{
    FMissionRosterEntry& Entry = Draft.Roster[RegimentIndex];
    const FText Number = MissionHudSlate::RegimentNumber(RegimentIndex);
    const FSimpleDelegate OnEdit = FSimpleDelegate::CreateSP(this, &SRegimentMissionHud::ClearPresetNotice);
    const auto ProfileName = [this, RegimentIndex, &Entry]
    {
        const auto* Faction = RosterFaction(RegimentIndex);
        if (!Faction)
            return FText::GetEmpty();
        if (const auto* Platform = RegimentEraCatalog::PlatformTroop(*Faction, Entry.Platform))
            return FText::FromString(Platform->Name);
        if (const auto* Infantry = Faction->InfantryProfiles.Find(Entry.InfantryProfile))
            return FText::FromString(Infantry->Troop.Name);
        const auto* Profile = Entry.bCavalry ? RegimentEraCatalog::MountedTroop(*Faction, Entry.MountedClass)
            : Entry.Archers > 0 ? RegimentEraCatalog::RangedTroop(*Faction, Entry.RangedClass) : nullptr;
        if (Profile)
            return FText::FromString(Profile->Name);
        return MissionHudSlate::HasPlatformChoices(Faction) || !Faction->InfantryProfiles.IsEmpty()
            ? LOCTEXT("ChooseTroopProfile", "Выбрать") : FText::GetEmpty();
    };
    return SNew(SBorder)
        .BorderImage(&MissionHudSlate::RosterRow)
        .Padding(FMargin(12, 4))
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, MissionHudSlate::ColumnGap, 0)
            [
                SNew(SBox).WidthOverride(MissionHudSlate::EnabledWidth).HAlign(HAlign_Center)
                [
                    SNew(SCheckBox)
                    .ToolTipText(LOCTEXT("EnableRegimentHelp", "Включить полк в миссию"))
                    .IsChecked_Lambda([&Entry] { return Entry.bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                    .OnCheckStateChanged_Lambda([this, &Entry](ECheckBoxState State)
                    {
                        Entry.bEnabled = State == ECheckBoxState::Checked;
                        ClearPresetNotice();
                    })
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, MissionHudSlate::ColumnGap, 0)
            [
                SNew(SBox).IsEnabled_Lambda([&Entry] { return Entry.bEnabled; })
                [
                    MissionHudSlate::MakeControl(TAttribute<FText>::CreateLambda([&Entry, Number]
                    {
                        return FText::Format(LOCTEXT("RegimentWithType", "{0} · {1}"), Number,
                            Entry.Platform != battle::BattlePlatform::None ? LOCTEXT("PlatformType", "Особый")
                            : Entry.bCavalry ? LOCTEXT("CavalryType", "Конный") : LOCTEXT("InfantryType", "Пеший"));
                    }), LOCTEXT("ChangeRegimentType", "Сменить тип полка"), MissionHudSlate::NameWidth, 36.f,
                        TAttribute<FSlateColor>::CreateLambda([&Entry]
                        {
                            return FSlateColor(Entry.bCavalry || Entry.Platform != battle::BattlePlatform::None
                                ? MissionHudSlate::SelectedTint : MissionHudSlate::ControlTint);
                        }),
                        FOnClicked::CreateLambda([this, &Entry]
                        {
                            if (Entry.Platform != battle::BattlePlatform::None)
                                MissionHudSlate::ResetPlatformRow(Entry, false);
                            else
                            {
                                Entry.bCavalry = !Entry.bCavalry;
                                Entry.InfantryProfile.Reset();
                            }
                            ClearPresetNotice();
                            return FReply::Handled();
                        }), true)
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, MissionHudSlate::ColumnGap, 0)
            [
                SNew(SWidgetSwitcher)
                .WidgetIndex_Lambda([&Entry] { return Entry.Platform == battle::BattlePlatform::None ? 0 : 1; })
                + SWidgetSwitcher::Slot()
                [
                    MissionHudSlate::MakeNumberInput(Entry, &FMissionRosterEntry::Swords, MissionMaxSwords,
                        TAttribute<FText>::CreateLambda([this, RegimentIndex]
                        {
                            const auto* Faction = RosterFaction(RegimentIndex);
                            const auto Name = Faction ? FText::FromString(Faction->Troops[int32(ERegimentEraRole::Swords)].Name)
                                : LOCTEXT("LegacySwords", "Мечники");
                            return FText::Format(LOCTEXT("TroopRange", "{0} · от 0 до {1}"), Name, MissionMaxSwords);
                        }), OnEdit)
                ]
                + SWidgetSwitcher::Slot()
                [
                    MissionHudSlate::MakeNumberInput(Entry, &FMissionRosterEntry::PlatformCount, MissionMaxPlatforms,
                        FText::Format(LOCTEXT("PlatformCountRange", "Единицы боевой техники · от 1 до {0}"),
                            MissionMaxPlatforms), OnEdit)
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, MissionHudSlate::ColumnGap, 0)
            [
                MissionHudSlate::MakeNumberInput(Entry, &FMissionRosterEntry::Archers, MissionMaxArchers,
                    LOCTEXT("ArchersRange", "Число стрелков выбранного типа в полку"), OnEdit)
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, MissionHudSlate::ColumnGap, 0)
            [
                MissionHudSlate::MakeNumberInput(Entry, &FMissionRosterEntry::Spearmen, MissionMaxSpecialists,
                    TAttribute<FText>::CreateLambda([this, RegimentIndex]
                    {
                        const auto* Faction = RosterFaction(RegimentIndex);
                        return Faction ? FText::FromString(Faction->Troops[int32(ERegimentEraRole::Spearmen)].Name)
                            : LOCTEXT("SpearmenRange", "Копейщики с длинным копьём");
                    }), OnEdit)
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, MissionHudSlate::ColumnGap, 0)
            [
                MissionHudSlate::MakeNumberInput(Entry, &FMissionRosterEntry::Mounted, MissionMaxSpecialists,
                    LOCTEXT("MountedRange", "Число всадников в конном полку"), OnEdit)
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SBox).WidthOverride(MissionHudSlate::TotalWidth).HAlign(HAlign_Center)
                [
                    MissionHudSlate::MakeText(TAttribute<FText>::CreateLambda([this, RegimentIndex]
                    {
                        const FMissionRosterEntry& Current = Draft.Roster[RegimentIndex];
                        return Current.bEnabled ? FText::AsNumber(Current.Total()) : LOCTEXT("InactiveTotal", "–");
                    }), 18, FSlateColor(MissionHudSlate::Ink), true)
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
            [
                SNew(SBox).WidthOverride(MissionHudSlate::ProfileWidth)
                .Visibility_Lambda([this] { return ModuleId.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
                [
                    SNew(SComboButton)
                    .IsEnabled_Lambda([this, RegimentIndex, &Entry]
                    {
                        const auto* Faction = RosterFaction(RegimentIndex);
                        return Entry.bEnabled && (Entry.bCavalry || Entry.Archers > 0 ||
                            MissionHudSlate::HasPlatformChoices(Faction) ||
                            (Faction && !Faction->InfantryProfiles.IsEmpty()));
                    })
                    .ToolTipText_Lambda([this, RegimentIndex, &Entry, ProfileName]
                    {
                        const auto* Faction = RosterFaction(RegimentIndex);
                        const auto* Profile = Faction ? RegimentEraCatalog::PlatformTroop(*Faction, Entry.Platform) : nullptr;
                        if (Profile && !Profile->Description.IsEmpty())
                            return FText::Format(LOCTEXT("PlatformProfileTooltip", "{0}\n{1}"),
                                ProfileName(), FText::FromString(Profile->Description));
                        return ProfileName();
                    })
                    .OnGetMenuContent(this, &SRegimentMissionHud::MakeRosterProfileMenu, RegimentIndex)
                    .ButtonContent()
                    [
                        SNew(STextBlock)
                        .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                        .Text_Lambda(ProfileName)
                    ]
                ]
            ]
        ];
}

void SRegimentMissionHud::ResetDeploymentPreview()
{
    if (!DeploymentPreview && DeploymentReport.IsEmpty())
        return;
    DeploymentPreview.Reset();
    DeploymentReport = FText::GetEmpty();
    Invalidate(EInvalidateWidgetReason::Layout);
}

FReply SRegimentMissionHud::RefreshDeploymentPreview()
{
    DeploymentPreview = BuildMissionAuthoringPreview(Draft, DeploymentReport);
    if (DeploymentPreview)
    {
        TArray<FString> Lines;
        for (const auto& Finding : DeploymentPreview->Diagnostics)
            Lines.Add(Finding.Message.ToString());
        DeploymentReport = Lines.IsEmpty() ? LOCTEXT("ClearDeploymentPreview", "Ошибок расстановки не найдено.")
                                           : FText::FromString(FString::Join(Lines, TEXT("\n")));
    }
    if (DeploymentMap)
        DeploymentMap->Invalidate(EInvalidateWidgetReason::Paint);
    Invalidate(EInvalidateWidgetReason::Layout);
    return FReply::Handled();
}

TSharedRef<SWidget> SRegimentMissionHud::MakeDeploymentPreview()
{
    using namespace MissionHudSlate;
    DeploymentMap = MakeMissionDeploymentMap(&DeploymentPreview);
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left)
        [
            MakeControl(LOCTEXT("CheckDeployment", "Проверить расстановку"),
                LOCTEXT("CheckDeploymentHelp", "Показать местность и фактические стартовые позиции полков."),
                255.f, 36.f, FSlateColor(ControlTint),
                FOnClicked::CreateSP(this, &SRegimentMissionHud::RefreshDeploymentPreview), true)
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 7, 0, 0)
        [
            SNew(SVerticalBox)
            .Visibility_Lambda([this] { return DeploymentPreview ? EVisibility::Visible : EVisibility::Collapsed; })
            + SVerticalBox::Slot().AutoHeight()
            [SNew(SBox).HeightOverride(262.f)[DeploymentMap.ToSharedRef()]]
            + SVerticalBox::Slot().AutoHeight()
            [
                MakeText(LOCTEXT("DeploymentLegend",
                    "Цветные контуры · фактический строй    Серые контуры полков · запрошенные старты    Золотой · цель"),
                    14, FSlateColor(Muted))
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
        [
            SNew(SBox).MaxDesiredHeight(115.f)
            .Visibility_Lambda([this] { return DeploymentReport.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
            [
                SNew(SScrollBox)
                + SScrollBox::Slot()
                [
                    SNew(STextBlock)
                    .Text_Lambda([this] { return DeploymentReport; })
                    .ColorAndOpacity(Brass)
                    .Font(FCoreStyle::GetDefaultFontStyle("Regular", 14))
                    .AutoWrapText(true)
                ]
            ]
        ];
}

TSharedRef<SWidget> SRegimentMissionHud::MakeDeploymentTable(int32 FirstIndex)
{
    using namespace MissionHudSlate;
    const bool bPlayer = FirstIndex == 0;
    const auto Table = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(12, 0, 12, 10)
        [
            MakeText(bPlayer ? LOCTEXT("BlueDeployment", "Синие полки") : LOCTEXT("OrangeDeployment", "Оранжевые полки"),
                19, FSlateColor(bPlayer ? Blue : Orange), true)
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(12, 0, 12, 7)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, ColumnGap, 0)
            [MakeColumnHeading(LOCTEXT("DeploymentSlot", "Полк"), DeploymentNumberWidth, HAlign_Center)]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, ColumnGap, 0)
            [MakeColumnHeading(LOCTEXT("DeploymentGroup", "Группа"), GroupWidth, HAlign_Center)]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, ColumnGap, 0)
            [MakeColumnHeading(LOCTEXT("DeploymentX", "Старт X, м"), CoordinateWidth, HAlign_Center)]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, ColumnGap, 0)
            [MakeColumnHeading(LOCTEXT("DeploymentY", "Старт Y, м"), CoordinateWidth, HAlign_Center)]
        ];
    for (int32 Index = FirstIndex; Index < FirstIndex + MissionRegimentsPerTeam; ++Index)
    {
        Table->AddSlot().AutoHeight().Padding(0, 0, 0, 4.f)
        [MakeDeploymentRow(Index)];
    }
    return Table;
}

TSharedRef<SWidget> SRegimentMissionHud::MakeDeploymentRow(int32 RegimentIndex)
{
    using namespace MissionHudSlate;
    FMissionRosterEntry& Entry = Draft.Roster[RegimentIndex];
    const auto MakeCoordinate = [this, &Entry, RegimentIndex](bool bX)
    {
        const auto Limit = [this, bX]
            {
            return (bX ? Draft.Field.HalfWidth() : Draft.Field.HalfHeight()) - MissionStartInset;
        };
        return MakeField<float>(CoordinateWidth,
            TAttribute<TOptional<float>>::CreateLambda([Limit]() -> TOptional<float> { return -Limit(); }),
            TAttribute<TOptional<float>>::CreateLambda([Limit]() -> TOptional<float> { return Limit(); }),
            TAttribute<FText>::CreateLambda([bX, Limit]
            {
                return FText::Format(bX ? LOCTEXT("StartXHelp", "Старт вдоль X. От {0} до {1} м.")
                    : LOCTEXT("StartYHelp", "Старт вдоль Y. От {0} до {1} м."), FText::AsNumber(-Limit()), FText::AsNumber(Limit()));
            }),
            TAttribute<float>::CreateLambda([this, &Entry, RegimentIndex, bX]
            {
                const auto Position = MissionStartingPosition(RegimentIndex, Entry, Draft.Field);
                return bX ? Position.x : Position.y;
            }),
            SSpinBox<float>::FOnValueChanged::CreateLambda([this, &Entry, bX](float Value)
            {
                auto& Coordinate = bX ? Entry.StartX : Entry.StartY;
                Coordinate = Value;
                ClearPresetNotice();
            }));
    };

    return SNew(SBorder)
        .BorderImage(&RosterRow)
        .IsEnabled_Lambda([&Entry] { return Entry.bEnabled; })
        .Padding(FMargin(12, 4))
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, ColumnGap, 0)
            [
                SNew(SBox).WidthOverride(DeploymentNumberWidth).HAlign(HAlign_Center)
                [MakeText(RegimentNumber(RegimentIndex), 18, FSlateColor(Ink), true)]
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, ColumnGap, 0)
            [
                MakeField<int32>(GroupWidth, 1, MissionGroupsPerTeam,
                    LOCTEXT("GroupHelp", "Полки одной стороны в одной группе подчиняются общему командиру"),
                    TAttribute<int32>::CreateLambda([&Entry, RegimentIndex] { return MissionGroup(RegimentIndex, Entry); }),
                    SSpinBox<int32>::FOnValueChanged::CreateLambda([this, &Entry](int32 Group)
                    {
                        Entry.Group = Group;
                        ClearPresetNotice();
                    }))
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, ColumnGap, 0)
            [MakeCoordinate(true)]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, ColumnGap, 0)
            [MakeCoordinate(false)]
            + SHorizontalBox::Slot().AutoWidth()
            [
                SNew(SBox).IsEnabled_Lambda([&Entry] { return Entry.StartX.has_value() || Entry.StartY.has_value(); })
                [
                    MakeControl(LOCTEXT("AutoStart", "Авто"), LOCTEXT("AutoStartHelp", "Вернуть стандартную точку полка"),
                        AutoPositionWidth, PresetControlHeight, FSlateColor(ControlTint),
                        FOnClicked::CreateLambda([this, &Entry]
                        {
                            Entry.StartX.reset();
                            Entry.StartY.reset();
                            ClearPresetNotice();
                            return FReply::Handled();
                        }), true)
                ]
            ]
        ];
}

TSharedRef<SWidget> SRegimentMissionHud::MakeMarchStatus()
{
    const auto Selectors = SNew(SHorizontalBox).Visibility(EVisibility::SelfHitTestInvisible);
    for (int32 Index = 0; Index < MissionRegimentsPerTeam; ++Index)
    {
        Selectors->AddSlot().AutoWidth().Padding(Index == 0 ? 0.f : 5.f, 0, 0, 0)
        [
            MakeRegimentSelector(uint64(Index + 1))
        ];
    }
    return SNew(SBorder)
        .BorderImage(&MissionHudSlate::Panel)
        .Visibility(EVisibility::SelfHitTestInvisible)
        .Padding(FMargin(16, 10))
        [
            SNew(SHorizontalBox)
            .Visibility(EVisibility::SelfHitTestInvisible)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 16, 0)
            [
                SNew(SBox)
                .Visibility(EVisibility::SelfHitTestInvisible)
                .WidthOverride(384.f)
                [
                    SNew(SVerticalBox)
                    .Visibility(EVisibility::SelfHitTestInvisible)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        MissionHudSlate::MakeText(TAttribute<FText>::CreateLambda([this] { return MissionHudSlate::MarchTitle(Stage); }),
                            19, TAttribute<FSlateColor>::CreateLambda([this]() -> FSlateColor
                            {
                                return Stage == EMissionHudStage::EnemyFound ? MissionHudSlate::Orange : MissionHudSlate::Brass;
                            }), true)
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
                    [
                        MissionHudSlate::MakeText(TAttribute<FText>::CreateLambda([this]
                            {
                                if (!MarchNotice.IsEmpty())
                                    return MarchNotice;
                                if (Stage == EMissionHudStage::EnemyFound)
                                    return bCanResume ? LOCTEXT("OrderReady", "Расставьте флажки и продолжите марш.")
                                        : LOCTEXT("OrderNeeded", "Поставьте флажок для одного из полков.");
                                return MissionHudSlate::MarchDetail(Stage);
                            }),
                            14, FSlateColor(MissionHudSlate::Muted))
                    ]
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                Selectors
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12, 0, 0, 0)
            [
                SNew(SBox)
                .Visibility_Lambda([this] { return Draft.bRegimentTactics && SelectedRegiment
                    ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
                [
                    MissionHudSlate::MakeControl(TAttribute<FText>::CreateLambda([this]
                    {
                        if (MarchCrossingPosition > 0)
                            return FText::Format(LOCTEXT("CrossingQueuePosition", "Переправа · № {0}"), MarchCrossingPosition);
                        if (MarchDeploymentSeconds > 0)
                            return FText::Format(LOCTEXT("DeployingMarchRegiment", "Развёртывание · {0} с"), MarchDeploymentSeconds);
                        return bTravelColumn ? LOCTEXT("MarchColumnAction", "Колонна · развернуть")
                                             : LOCTEXT("MarchLineAction", "Линия · в колонну");
                    }), LOCTEXT("MarchPostureHint", "Колонна идёт быстрее. Линия готова к столкновению. Повторное нажатие отменяет развёртывание."),
                        227.f, 40.f, FSlateColor(MissionHudSlate::ControlTint), FOnClicked::CreateLambda([this]
                        {
                            if (OnToggleMarchPosture)
                                OnToggleMarchPosture();
                            return FReply::Handled();
                        }), false)
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8, 0, 0, 0)
            [
                SNew(SBox)
                .Visibility_Lambda([this] { return Stage == EMissionHudStage::EnemyFound
                    ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; })
                .IsEnabled_Lambda([this] { return bCanResume; })
                [
                    MissionHudSlate::MakeControl(LOCTEXT("ResumeMarch", "Продолжить марш"),
                        LOCTEXT("ResumeMarchHint", "Отправить полки по указанным маршрутам"),
                        210.f, 40.f, FSlateColor(MissionHudSlate::SelectedTint), FOnClicked::CreateLambda([this]
                        {
                            if (OnResumeMarch)
                                OnResumeMarch();
                            return FReply::Handled();
                        }), false)
                ]
            ]
        ];
}

TSharedRef<SWidget> SRegimentMissionHud::MakeRegimentSelector(uint64 RegimentId)
{
    const FText Number = MissionHudSlate::RegimentNumber(int32(RegimentId) - 1);
    return SNew(SBox)
        .Visibility_Lambda([this, RegimentId]
        {
            return Draft.Roster[int32(RegimentId) - 1].bEnabled ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
        })
        [
            MissionHudSlate::MakeControl(Number,
                FText::Format(LOCTEXT("SelectRegiment", "Выбрать синий полк {0}"), Number), 52.f, 40.f,
                TAttribute<FSlateColor>::CreateLambda([this, RegimentId]() -> FSlateColor
                {
                    return SelectedRegiment == RegimentId ? MissionHudSlate::SelectedTint : MissionHudSlate::ControlTint;
                }),
                FOnClicked::CreateLambda([this, RegimentId]
                {
                    if (OnSelectRegiment)
                        OnSelectRegiment(RegimentId);
                    return FReply::Handled();
                }), false)
        ];
}

TSharedRef<SWidget> SRegimentMissionHud::MakeMarchHelp()
{
    return SNew(SBorder)
        .BorderImage(&MissionHudSlate::Panel)
        .Visibility(EVisibility::SelfHitTestInvisible)
        .Padding(FMargin(12, 7, 7, 7))
        [
            SNew(SHorizontalBox)
            .Visibility(EVisibility::SelfHitTestInvisible)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 14, 0)
            [
                MissionHudSlate::MakeText(TAttribute<FText>::CreateLambda([this] {
                    if (Draft.bRegimentTactics && battle::MarchHasWaterCrossings(Draft.Terrain))
                        return LOCTEXT("MarchCrossingHelp", "ЛКМ — полк или маршрут  ·  Shift — группа  ·  Ctrl + ЛКМ за рекой — очередь и развёртывание");
                    return LOCTEXT("MarchHelp", "ЛКМ по полку — выбрать  ·  ЛКМ по карте — маршрут  ·  Shift + ЛКМ — всей группе");
                }),
                    14, FSlateColor(MissionHudSlate::Muted))
            ]
            + SHorizontalBox::Slot().AutoWidth()
            [
                MissionHudSlate::MakeControl(LOCTEXT("ReturnToSetup", "К составу"),
                    LOCTEXT("ReturnToSetupHelp", "Завершить поход и вернуться к выбору состава (Esc)"),
                    128.f, 36.f, FSlateColor(MissionHudSlate::ControlTint), FOnClicked::CreateLambda([this]
                    {
                        if (OnReturnToSetup)
                        {
                            OnReturnToSetup();
                        }
                        return FReply::Handled();
                    }), false)
            ]
        ];
}

#undef LOCTEXT_NAMESPACE
