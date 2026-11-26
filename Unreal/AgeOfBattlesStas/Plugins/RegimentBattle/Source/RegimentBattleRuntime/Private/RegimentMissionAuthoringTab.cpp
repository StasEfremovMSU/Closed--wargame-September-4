#include "RegimentMissionAuthoringTab.h"

#if WITH_EDITOR

#include "SRegimentMissionDeploymentMap.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/Paths.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RegimentMissionAuthoringTab"

namespace
{
const FName TabName(TEXT("RegimentMissionAuthoring"));
FDelegateHandle MenuHandle;

struct FPreviewPreset
{
    FMissionPresetListItem Item;
    FString Directory;

    FText Label() const
    {
        return FText::FromString(Item.Name);
    }
};

class SMissionAuthoringPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SMissionAuthoringPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&)
    {
        ReadPresetList();
        Map = MakeMissionDeploymentMap(&Preview);
        ChildSlot
        [
            SNew(SBorder).Padding(14)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0, 0, 8, 0)
                    [
                        SAssignNew(Combo, SComboBox<TSharedPtr<FPreviewPreset>>)
                        .OptionsSource(&Options)
                        .OnGenerateWidget_Lambda([](const TSharedPtr<FPreviewPreset>& Option)
                            { return SNew(STextBlock).Text(Option->Label()); })
                        .OnSelectionChanged_Lambda([this](TSharedPtr<FPreviewPreset> Value, ESelectInfo::Type)
                            { Selected = MoveTemp(Value); LoadSelected(); })
                        [
                            SNew(STextBlock).Text_Lambda([this]
                                { return Selected ? Selected->Label() : LOCTEXT("Select", "Выберите сценарий"); })
                        ]
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton).Text(LOCTEXT("Refresh", "Обновить с диска"))
                        .OnClicked_Lambda([this]
                        {
                            const auto Old = Selected;
                            ReadPresetList();
                            Combo->RefreshOptions();
                            const auto* Match = Old ? Options.FindByPredicate([&Old](const auto& Option)
                                { return Option->Item.Id == Old->Item.Id && Option->Directory == Old->Directory; }) : nullptr;
                            Select(Match ? *Match : Options.IsEmpty() ? nullptr : Options[0]);
                            return FReply::Handled();
                        })
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
                [SNew(STextBlock).Text_Lambda([this] { return Identity; }).AutoWrapText(true)]
                + SVerticalBox::Slot().AutoHeight()
                [SNew(STextBlock).Text_Lambda([this] { return Summary; }).AutoWrapText(true)]
                + SVerticalBox::Slot().FillHeight(1.0f).Padding(0, 6)
                [
                    Map.ToSharedRef()
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [
                    SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("Legend",
                        "Синий и оранжевый · строй и направление полка    Серый контур полка · запрошенный старт    "
                        "Золотой · цель    Серый участок · стена    Синий участок · вода    Светлый грунт · подъём"))
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SBox).MaxDesiredHeight(154.f)
                    [
                        SNew(SScrollBox)
                        + SScrollBox::Slot()
                        [SNew(STextBlock).Text_Lambda([this] { return Diagnostics; }).AutoWrapText(true)]
                    ]
                ]
            ]
        ];
        if (!Options.IsEmpty())
            Select(Options[0]);
    }

private:
    void Select(TSharedPtr<FPreviewPreset> Value)
    {
        Combo->SetSelectedItem(Value);
        if (Selected != Value)
        {
            Selected = MoveTemp(Value);
            LoadSelected();
        }
    }

    void ReadPresetList()
    {
        Options.Reset();
        const auto Historical = FPaths::ProjectContentDir() / TEXT("Battle/HistoricalPresets");
        const auto Saved = RegimentMissionPresets::Directory();
        TSet<FGuid> FilePresets;
        for (auto Item : RegimentMissionPresets::List(Historical))
        {
            FilePresets.Add(Item.Id);
            Options.Add(MakeShared<FPreviewPreset>(FPreviewPreset{MoveTemp(Item), Historical}));
        }
        for (auto Item : RegimentMissionPresets::List(Saved))
            Options.Add(MakeShared<FPreviewPreset>(FPreviewPreset{MoveTemp(Item), Saved}));
        for (auto Item : RegimentMissionPresets::BuiltIns())
            if (!FilePresets.Contains(Item.Id))
                Options.Add(MakeShared<FPreviewPreset>(FPreviewPreset{MoveTemp(Item), FString()}));
    }

    void LoadSelected()
    {
        Preview.Reset();
        Summary = FText::GetEmpty();
        Identity = FText::GetEmpty();
        Diagnostics = FText::GetEmpty();
        if (Map)
            Map->Invalidate(EInvalidateWidgetReason::Paint);
        if (!Selected)
            return;
        FMissionPreset Preset;
        FText Error;
        const FGuid Id = Selected->Item.Id;
        const bool bFromFile = !Selected->Directory.IsEmpty();
        const FString Identifier = bFromFile
            ? FPaths::ConvertRelativePathToFull(Selected->Directory / (Id.ToString(EGuidFormats::Digits) + TEXT(".json")))
            : Id.IsValid() ? Id.ToString(EGuidFormats::Digits) : FString(TEXT("Default"));
        Identity = FText::AsCultureInvariant(Identifier);
        const bool bLoaded = bFromFile ? RegimentMissionPresets::Load(Selected->Directory, Id, Preset, Error)
                                     : RegimentMissionPresets::LoadBuiltIn(Id, Preset);
        if (bLoaded)
            Preview = BuildMissionAuthoringPreview(Preset, Error);
        if (!Preview)
        {
            Summary = LOCTEXT("Invalid", "Расстановка недоступна");
            Diagnostics = Error.IsEmpty() ? LOCTEXT("Unloadable", "Не удалось открыть сценарий.") : Error;
            return;
        }
        Summary = FText::Format(LOCTEXT("MapSummary", "{0} × {1} м · {2} полков · {3}"),
            Preset.Field.width_m, Preset.Field.height_m, Preview->Spawns.Num(),
            Preview->bTactical ? LOCTEXT("BattleStart", "старт в бою") : LOCTEXT("MarchStart", "начало марша"));
        TArray<FString> Lines;
        for (const auto& Finding : Preview->Diagnostics)
            Lines.Add(Finding.Message.ToString());
        Diagnostics = Lines.IsEmpty() ? LOCTEXT("Clear", "Ошибок расстановки не найдено.")
            : FText::FromString(FString::Join(Lines, TEXT("\n")));
    }

    TArray<TSharedPtr<FPreviewPreset>> Options;
    TSharedPtr<FPreviewPreset> Selected;
    TSharedPtr<SComboBox<TSharedPtr<FPreviewPreset>>> Combo;
    TSharedPtr<SWidget> Map;
    TOptional<FMissionAuthoringPreview> Preview;
    FText Identity, Summary, Diagnostics;
};
}

void RegimentMissionAuthoringTab::Register()
{
    if (IsRunningCommandlet())
        return;
    const auto TabLabel = LOCTEXT("TabLabel", "Сценарии Regiment Battle");
    FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName,
        FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
        {
            return SNew(SDockTab).TabRole(ETabRole::NomadTab)
                [SNew(SMissionAuthoringPanel)];
        })).SetDisplayName(TabLabel).SetMenuType(ETabSpawnerMenuType::Hidden);
    MenuHandle = UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([TabLabel]
    {
        FToolMenuOwnerScoped Owner(TabName);
        auto& Section = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools")->FindOrAddSection("RegimentBattle");
        Section.AddMenuEntry(TabName, TabLabel, LOCTEXT("OpenPreview", "Проверить расстановку полков и посмотреть поле боя."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([] { FGlobalTabmanager::Get()->TryInvokeTab(TabName); })));
    }));
}

void RegimentMissionAuthoringTab::Unregister()
{
    if (!MenuHandle.IsValid())
        return;
    UToolMenus::UnRegisterStartupCallback(MenuHandle);
    UToolMenus::UnregisterOwner(TabName);
    FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
    MenuHandle.Reset();
}

#undef LOCTEXT_NAMESPACE
#endif
